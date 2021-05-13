#include "nrf_cli.h"
#include "zboss_api.h"
#include "zb_error_handler.h"
#include "zigbee_cli.h"
#include "zigbee_cli_utils.h"

/**
 * @brief Create or remove a binding between two endpoints on two nodes.
 *
 * @code
 * nwk concentrator {enable,disable} <d:radius> <d:disc_time>
 * @endcode
 * Example:
 * @code
 * nwk concentrator enable 0 5
 * @endcode
 *
 */
static void cmd_zb_concentrator(nrf_cli_t const *p_cli, size_t argc, char **argv)
{
    zb_bool_t enable;
    zb_uint8_t radius = 0;
    zb_uint32_t disc_time = 0;

    if (strcmp(argv[0], "enable") == 0)
    {
        enable = ZB_TRUE;
    }
    else
    {
        enable = ZB_FALSE;
    }

    if (nrf_cli_help_requested(p_cli) || (argc == 1))
    {
        print_usage(p_cli, argv[0],
                    "<d:radius> <d:disc_time>"
                    "\r\nradius:     The hop count radius for concentrator route discoveries."
                    "\r\n            If the value is set zero then the default radius will be used."
                    "\r\ndisc_time:  The time in seconds between concentrator route discoveries."
                    "\r\n            If the value is set to zero, the route discoveries are done by the application layer only.");
        return;
    }

    if (argc != 3)
    {
        print_error(p_cli, "Incorrect number of arguments", ZB_FALSE);
        return;
    }

    if (!sscan_uint8(argv[1], &radius))
    {
        print_error(p_cli, "Incorrect radius", ZB_FALSE);
        return;
    }
    else if (radius > 30)
    {
        print_error(p_cli, "Only radius from 0 to 30 are supported", ZB_FALSE);
        return;
    }

    if (!sscanf(argv[2], "%d", &disc_time))
    {
        print_error(p_cli, "Incorrect disc_time", ZB_FALSE);
        return;
    }

    if (!zb_cli_is_stack_started())
    {
        print_error(p_cli, "Stack not start", ZB_FALSE);
        return;
    }

    if (zb_get_network_role() != ZB_NWK_DEVICE_TYPE_COORDINATOR)
    {
        print_error(p_cli, "Only coordinator supported", ZB_FALSE);
        return;
    }

    if (enable)
    {
        zb_start_concentrator_mode(radius, disc_time);
    }
    else
    {
        zb_stop_concentrator_mode();
    }
    print_done(p_cli, ZB_TRUE);
}

NRF_CLI_CREATE_STATIC_SUBCMD_SET(m_sub_concentrator){
    NRF_CLI_CMD(enable, NULL, "enable Concentrator mode", cmd_zb_concentrator),
    NRF_CLI_CMD(disable, NULL, "disable Concentrator mode", cmd_zb_concentrator),
    NRF_CLI_SUBCMD_SET_END};

NRF_CLI_CREATE_STATIC_SUBCMD_SET(m_sub_nwk){
    NRF_CLI_CMD(concentrator, &m_sub_concentrator, "enable or disable Concentrator mode", NULL),
    NRF_CLI_SUBCMD_SET_END};

NRF_CLI_CMD_REGISTER(nwk, &m_sub_nwk, "NWK manipulation", NULL);