
#include "sdk_config.h"
#include "zboss_api.h"
#include "zboss_api_addons.h"

#ifdef ZIGBEE_MEM_CONFIG_MODEL
#if (ZIGBEE_MEM_CONFIG_MODEL == 0)
/* None of the files zb_mem_config_*.h included, use default memory settings */
#elif (ZIGBEE_MEM_CONFIG_MODEL == 1)
#include "zb_mem_config_min.h"
#elif (ZIGBEE_MEM_CONFIG_MODEL == 2)
#include "zb_mem_config_med.h"
#elif (ZIGBEE_MEM_CONFIG_MODEL == 3)
#include "zb_mem_config_max.h"
#else
#error ZIGBEE_MEM_CONFIG_MODEL unsupported value, please check sdk_config.h
#endif
#endif

#include "zb_error_handler.h"
#include "zb_ha_configuration_tool.h"
#include "zigbee_cli.h"
#include "zigbee_helpers.h"

#include "app_timer.h"
#include "boards.h"
#include "nrf_drv_clock.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

/**< Scan only one, predefined channel to find the coordinator. */
#define IEEE_CHANNEL_MASK (1l << ZIGBEE_CHANNEL)

/**< Do not erase NVRAM to save the network parameters after device reboot or
	 power-off. NOTE: If this option is set to ZB_TRUE then do full device erase for
	 all network devices before running other samples.
*/
#define ERASE_PERSISTENT_CONFIG ZB_FALSE

/**< LED indicating that light switch successfully joind Zigbee network. */
#define ZIGBEE_NETWORK_STATE_LED BSP_BOARD_LED_0

#define CLI_APPLICATION_VERSION 1
#define CLI_STACK_VERSION 0
#define CLI_HW_VERSION 1
#define CLI_MANUFACTURER_NAME "NTTai"
#define CLI_MODEL_IDENTIFIER "CLI-AGENT-ROUTER"
#define CLI_DATE_CODE "20201010"
#define CLI_POWER_SOURCE (ZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN)
#define CLI_LOCATION_DESCRIPTION "Unknown"
#define CLI_PHYSICAL_ENVIRONMENT (ZB_ZCL_BASIC_ENV_UNSPECIFIED)
#define CLI_SW_BUILD_ID "0000000"

#if !defined ZB_ROUTER_ROLE
#error Define ZB_ROUTER_ROLE to compile CLI agent (Router) source code.
#endif

static zb_zcl_basic_attrs_ext_t m_basic_attrs_ext;
static zb_uint16_t m_attr_identify_time = 0;

/* Declare attribute list for Basic cluster. */
/* Declare attribute list for Basic cluster. */
ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST_EXT(
	basic_attr_list, &m_basic_attrs_ext.zcl_version,
	&m_basic_attrs_ext.app_version, &m_basic_attrs_ext.stack_version,
	&m_basic_attrs_ext.hw_version, m_basic_attrs_ext.mf_name,
	m_basic_attrs_ext.model_id, m_basic_attrs_ext.date_code,
	&m_basic_attrs_ext.power_source, m_basic_attrs_ext.location_id,
	&m_basic_attrs_ext.ph_env, m_basic_attrs_ext.sw_ver);

/* Declare attribute list for Identify cluster. */
ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(identify_attr_list, &m_attr_identify_time);

/* Declare cluster list for CLI Agent device. */
/* Only clusters Identify and Basic have attributes. */
ZB_HA_DECLARE_CONFIGURATION_TOOL_CLUSTER_LIST(cli_agent_clusters,
					      basic_attr_list,
					      identify_attr_list);

/* Declare endpoint for CLI Agent device. */
ZB_HA_DECLARE_CONFIGURATION_TOOL_EP(cli_agent_ep, ZIGBEE_CLI_ENDPOINT,
				    cli_agent_clusters);

/* Declare application's device context (list of registered endpoints) for CLI Agent device. */
ZB_HA_DECLARE_CONFIGURATION_TOOL_CTX(cli_agent_ctx, cli_agent_ep);

static zb_void_t zb_cli_init_cluster_attr(zb_void_t)
{
	/* Basic cluster attributes data */
	m_basic_attrs_ext.zcl_version = ZB_ZCL_VERSION;
	m_basic_attrs_ext.app_version = CLI_APPLICATION_VERSION;
	m_basic_attrs_ext.stack_version = CLI_STACK_VERSION;
	m_basic_attrs_ext.hw_version = CLI_HW_VERSION;

	/* Use ZB_ZCL_SET_STRING_VAL to set strings, because the first byte should
	 * contain string length without trailing zero.
	 *
	 * For example "test" string wil be encoded as:
	 *   [(0x4), 't', 'e', 's', 't']
	 */
	ZB_ZCL_SET_STRING_VAL(m_basic_attrs_ext.mf_name, CLI_MANUFACTURER_NAME,
			      ZB_ZCL_STRING_CONST_SIZE(CLI_MANUFACTURER_NAME));

	ZB_ZCL_SET_STRING_VAL(m_basic_attrs_ext.model_id, CLI_MODEL_IDENTIFIER,
			      ZB_ZCL_STRING_CONST_SIZE(CLI_MODEL_IDENTIFIER));

	ZB_ZCL_SET_STRING_VAL(m_basic_attrs_ext.date_code, CLI_DATE_CODE,
			      ZB_ZCL_STRING_CONST_SIZE(CLI_DATE_CODE));

	m_basic_attrs_ext.power_source = CLI_POWER_SOURCE;

	ZB_ZCL_SET_STRING_VAL(
		m_basic_attrs_ext.location_id, CLI_LOCATION_DESCRIPTION,
		ZB_ZCL_STRING_CONST_SIZE(CLI_LOCATION_DESCRIPTION));

	m_basic_attrs_ext.ph_env = CLI_PHYSICAL_ENVIRONMENT;

	/* Add project version to zigbee basic sw version attr */
	ZB_ZCL_SET_STRING_VAL(m_basic_attrs_ext.sw_ver, CLI_SW_BUILD_ID,
			      ZB_ZCL_STRING_CONST_SIZE(CLI_SW_BUILD_ID));
}

static void log_init(void)
{
	ret_code_t err_code = NRF_LOG_INIT(NULL);
	APP_ERROR_CHECK(err_code);

	NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief Zigbee stack event handler.
 *
 * @param[in]   bufid   Reference to the Zigbee stack buffer used to pass signal.
 */
void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t *p_sg_p = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &p_sg_p);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

	/* Update network status LED */
	zigbee_led_status_update(bufid, ZIGBEE_NETWORK_STATE_LED);

	switch (sig) {
	case ZB_ZDO_SIGNAL_LEAVE:
		/** Handle the LEAVE signal inside main instead of using default signal handler
	      * so the CLI will not attempt to rejoin the network after it receives the LEAVE command
		  */
		if (status == RET_OK) {
			zb_zdo_signal_leave_params_t *p_leave_params =
				ZB_ZDO_SIGNAL_GET_PARAMS(
					p_sg_p, zb_zdo_signal_leave_params_t);
			NRF_LOG_INFO("Network left (leave type: %d)",
				     p_leave_params->leave_type);
		} else {
			NRF_LOG_ERROR("Unable to leave network (status: %d)",
				      status);
		}
		zb_enable_auto_pan_id_conflict_resolution(ZB_FALSE);
		break;

	default:
		/* Call default signal handler. */
		ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
		break;
	}

	if (bufid) {
		zb_buf_free(bufid);
	}
}

/**@brief Function for application main entry.
 */
int main(void)
{
	ret_code_t ret;
	zb_ieee_addr_t ieee_addr;

	/* Intiialise the leds */
	bsp_board_init(BSP_INIT_LEDS);
	bsp_board_leds_off();

	/* Initialize loging system and GPIOs. */
	log_init();

#if defined(APP_USBD_ENABLED) && APP_USBD_ENABLED
	ret = nrf_drv_clock_init();
	APP_ERROR_CHECK(ret);
	nrf_drv_clock_lfclk_request(NULL);
#endif

	ret = app_timer_init();
	APP_ERROR_CHECK(ret);

	/* Initialize the Zigbee CLI subsystem */
	zb_cli_init(ZIGBEE_CLI_ENDPOINT);

	/* Set Zigbee stack logging level and traffic dump subsystem. */
	ZB_SET_TRACE_LEVEL(ZIGBEE_TRACE_LEVEL);
	ZB_SET_TRACE_MASK(ZIGBEE_TRACE_MASK);
	ZB_SET_TRAF_DUMP_OFF();

	/* Initialize Zigbee stack. */
	ZB_INIT("cli_agent_router");

	/* Set device address to the value read from FICR registers. */
	zb_osif_get_ieee_eui64(ieee_addr);
	zb_set_long_address(ieee_addr);

	zb_set_bdb_primary_channel_set(IEEE_CHANNEL_MASK);
	zigbee_erase_persistent_storage(ERASE_PERSISTENT_CONFIG);

	/* Register CLI Agent device context (endpoints). */
	ZB_AF_REGISTER_DEVICE_CTX(&cli_agent_ctx);

	/* Set the endpoint receive hook */
	ZB_AF_SET_ENDPOINT_HANDLER(ZIGBEE_CLI_ENDPOINT, cli_agent_ep_handler);

	/* Init default val for cluster attribute */
	zb_cli_init_cluster_attr();

	/* Start Zigbee CLI subsystem. */
	zb_cli_start();

	/* Start Zigbee stack. */
	while (1) {
		if (zb_cli_is_stack_started()) {
#ifdef ZIGBEE_CLI_DEBUG
			if (!zb_cli_stack_is_suspended()) {
				zboss_main_loop_iteration();
			}
#else
			zboss_main_loop_iteration();
#endif
		}
		UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());
		UNUSED_RETURN_VALUE(zb_cli_process());
	}
}

/**
 * @}
 */
