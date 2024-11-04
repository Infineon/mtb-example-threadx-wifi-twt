/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the TWT Demo Example
*              for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2021-2024, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/* Header file includes. */
#include "cyhal.h"
#include "cybsp.h"
#include "cybsp_wifi.h"
#include "cy_retarget_io.h"

/* RTOS header file. */
#include "cyabs_rtos.h"

/* command console header files. */
#include "command_console.h"
#include "wifi_utility.h"
#include "iperf_utility.h"

/* Wi-Fi connection manager header file. */
#include "cy_wcm.h"

/* WHD header file. */
#include "whd_wlioctl.h"

/* Standard C header files. */
#include <inttypes.h>


/*******************************************************************************
* Macros
********************************************************************************/
#define THREAD_STACK 4*1024

#define CONSOLE_COMMAND_MAX_PARAMS      (32)
#define CONSOLE_COMMAND_MAX_LENGTH      (85)
#define CONSOLE_COMMAND_HISTORY_LENGTH  (10)
 
#define WIFI_SSID                       ""
#define WIFI_KEY                        ""
#define WIFI_SECURITY                   CY_WCM_SECURITY_WPA2_AES_PSK
#define WIFI_BAND                       CY_WCM_WIFI_BAND_ANY
#define MAX_WIFI_CONN_RETRIES           15

#define IP_STR_LEN                      16
#define WDT_TIMEOUT_MS                  (4000)
#define CY_RSLT_ERROR                   ( -1 )


/*******************************************************************************
* Global Variables
********************************************************************************/
static cy_thread_t nxs_thread;
static uint64_t nxs_stack[(THREAD_STACK)/sizeof(uint64_t)];

/* wcm parameters */
static cy_wcm_config_t wcm_config;
static cy_wcm_connect_params_t conn_params;

static cy_timer_t wdt_timer_t;

const char* console_delimiter_string = " ";
static char command_buffer[CONSOLE_COMMAND_MAX_LENGTH];
static char command_history_buffer[CONSOLE_COMMAND_MAX_LENGTH * CONSOLE_COMMAND_HISTORY_LENGTH];

extern whd_interface_t whd_ifs[2];


/*******************************************************************************
* Function Prototypes
********************************************************************************/
cy_rslt_t ConnectWifi(cy_wcm_itwt_profile_t profile);

int itwt_setup(int argc, char* argv[], tlv_buffer_t** data);
int itwt_teardown(int argc, char* argv[], tlv_buffer_t** data);

#if defined(H1CP_CLOCK_FREQ)
#if (CYHAL_API_VERSION >= 2)
cy_rslt_t set_cpu_clock_cp ( uint32_t freq, cyhal_clock_t *obj );
#endif
#endif

cy_rslt_t command_console_add_command();


/* iTWT related */
#define ITWT_COMMANDS \
    { (char *) "itwt_setup", itwt_setup, 1, NULL, NULL, (char *) "<profile> <active|idle>", (char *) "Setup an iTWT session with parameters as per selected iTWT profile" }, \
    { (char *) "itwt_teardown", itwt_teardown, 0, NULL, NULL, (char *) "", (char *) "Teardown ongoing iTWT session" }, \

const cy_command_console_cmd_t itwt_commands_table[] =
{
    ITWT_COMMANDS
    CMD_TABLE_END
};


/*******************************************************************************
* Function Name: get_ip_string
********************************************************************************
* Summary:
* This function converts ip address from integer to string format
*   
*
* Parameters:
*  uint32_t ip   : ip address in integer format
*  char* buffer  : buffer to store ip address string
*
* Return:
*  void
*
*******************************************************************************/
static void get_ip_string(char* buffer, uint32_t ip)
{
    sprintf(buffer, "%lu.%lu.%lu.%lu",
            (unsigned long)(ip      ) & 0xFF,
            (unsigned long)(ip >>  8) & 0xFF,
            (unsigned long)(ip >> 16) & 0xFF,
            (unsigned long)(ip >> 24) & 0xFF);
}


/*******************************************************************************
* Function Name: itwt_setup
********************************************************************************
* Summary:
* This function sets up an iTWT session with AP as per user selected iTWT profile
*    
*
* Parameters:
*  int argc
*  char* argv[]
*  tlv_buffer_t** data
*
* Return:
*  int
*
*******************************************************************************/
int itwt_setup(int argc, char* argv[], tlv_buffer_t** data)
{
    cy_rslt_t result;

    if(argc < 1)
    {
        printf("Insufficient number of arguments. Command format: itwt_setup <profile>\n");
        return -1;
    }

    if(cy_wcm_is_connected_to_ap())
    {
        printf("Already connected. Disconnecting from AP!!\n");
        if((result = cy_wcm_disconnect_ap()) != CY_RSLT_SUCCESS){
            printf("Failed to disconnect from AP! Error code: 0x%08" PRIx32 "\n", result);
            return result;
        }
    }

    if(!strcmp(argv[1], "idle"))
    {
        result = ConnectWifi(CY_WCM_ITWT_PROFILE_IDLE);
    }
    else if(!strcmp(argv[1], "active"))
    {
        result = ConnectWifi(CY_WCM_ITWT_PROFILE_ACTIVE);
    }
    else
    {
        printf("Invalid Profile\n");
        return -1;
    }

    return result;
}


/*******************************************************************************
* Function Name: itwt_teardown
********************************************************************************
* Summary:
* This function tearsdown the ongoing TWT session
*
*
* Parameters:
*  int argc
*  char* argv[]
*  tlv_buffer_t** data
*
* Return:
*  int
*
*******************************************************************************/
int itwt_teardown(int argc, char* argv[], tlv_buffer_t** data)
{
    cy_rslt_t result;

    whd_twt_teardown_params_t twt_params;

    twt_params.negotiation_type = TWT_CTRL_NEGO_TYPE_0;
    twt_params.flow_id = 0;
    twt_params.bcast_twt_id = 0;
    twt_params.teardown_all_twt = 0;

    result = whd_wifi_twt_teardown(whd_ifs[CY_WCM_INTERFACE_TYPE_STA], &twt_params);
    if(result != CY_RSLT_SUCCESS)
    {
        printf("TWT session teardown failed! Error code: 0x%08" PRIx32 "\n", result);
    }

     return result;
}


/*******************************************************************************
* Function Name: ConnectWifi
********************************************************************************
* Summary:
* This function initiates connection to the Wi-Fi Access Point using the 
* specified SSID and KEY. The connection is retried a maximum of 
* 'MAX_WIFI_CONN_RETRIES' times with interval of 500 milliseconds.
*
*
* Parameters:
*  cy_wcm_itwt_profile_t profile : iTWT <Profile> <active|idle>
*
* Return:
*  cy_rslt_t : CY_RSLT_SUCCESS upon a successful Wi-Fi connection, else an 
*              error code indicating the failure.
*
*******************************************************************************/
cy_rslt_t ConnectWifi(cy_wcm_itwt_profile_t profile)
{
    cy_rslt_t result ;

    const char *ssid = WIFI_SSID ;
    const char *key = WIFI_KEY ;
    cy_wcm_wifi_band_t band = WIFI_BAND;
    int retry_count = 0;
    cy_wcm_ip_address_t ip_addr;
    char ipstr[IP_STR_LEN];

    memset(&conn_params, 0, sizeof(cy_wcm_connect_params_t));

    printf("Connecting to Wi-Fi Network: %s\n", WIFI_SSID);

    while (1)
    {
        /*
        * Join to WIFI AP
        */
        memcpy(&conn_params.ap_credentials.SSID, ssid, strlen(ssid) + 1);
        memcpy(&conn_params.ap_credentials.password, key, strlen(key) + 1);
        conn_params.ap_credentials.security = WIFI_SECURITY;
        conn_params.band = band;
        conn_params.itwt_profile = profile;

        result = cy_wcm_connect_ap(&conn_params, &ip_addr);
        cy_rtos_delay_milliseconds(500);

        if(result != CY_RSLT_SUCCESS)
        {
            retry_count++;
            if (retry_count >= MAX_WIFI_CONN_RETRIES)
            {
                printf("Exceeded max WiFi connection attempts\n");
                return CY_RSLT_ERROR;
            }
            printf("Connection to WiFi network failed. Retrying...\n");
            continue;
        }
        else
        {
            printf("Successfully connected to Wi-Fi network '%s'.\n", ssid);
            get_ip_string(ipstr, ip_addr.ip.v4);
            printf("IP Address %s assigned\n", ipstr);
            break;
        }
    }

    return CY_RSLT_SUCCESS;
}


/*******************************************************************************
* Function Name: command_console_add_command
********************************************************************************
* Summary:
* This function does the following: 
*    1. Initializes command console library
*    2. Initializes Wi-Fi uitlity and adds Wi-Fi commands
*    3. Initializes iperf uitlity and adds iperf commands
*    4. Registers itwt commands table
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t
*
*******************************************************************************/
cy_rslt_t command_console_add_command(void) {

    cy_command_console_cfg_t console_cfg;
    cy_rslt_t result = CY_RSLT_SUCCESS;

    console_cfg.serial             = (void *)&cy_retarget_io_uart_obj;
    console_cfg.line_len           = sizeof(command_buffer);
    console_cfg.buffer             = command_buffer;
    console_cfg.history_len        = CONSOLE_COMMAND_HISTORY_LENGTH;
    console_cfg.history_buffer_ptr = command_history_buffer;
    console_cfg.delimiter_string   = console_delimiter_string;
    console_cfg.params_num         = CONSOLE_COMMAND_MAX_PARAMS;
    console_cfg.thread_priority    = CY_RTOS_PRIORITY_NORMAL;
    console_cfg.delimiter_string   = " ";

    /* Initialize command console library */
    result = cy_command_console_init(&console_cfg);
    if ( result != CY_RSLT_SUCCESS )
    {
        printf("Error in initializing command console library : 0x%08" PRIx32 "\n", result);
        goto error;
    }

    /* Initialize Wi-Fi utility and add Wi-Fi commands */
    result = wifi_utility_init();
    if ( result != CY_RSLT_SUCCESS )
    {
        printf("Error in initializing command console library : 0x%08" PRIx32 "\n", result);
        goto error;
    }

    /* Initialize IPERF utility and add IPERF commands */
    iperf_utility_init(&wcm_config.interface);

    /* Register itwt commands table */
    result = cy_command_console_add_table(itwt_commands_table);
    if ( result != CY_RSLT_SUCCESS )
    {
        printf("Error in adding command console table : 0x%08" PRIx32 "\n", result);
        goto error;
    }

    return CY_RSLT_SUCCESS;

error:
    return CY_RSLT_ERROR;

}


/*******************************************************************************
* Function Name: wdt_handler
********************************************************************************
* Summary:
* This is the callback function of wdt_timer_t timer. This pets the watchdog
* timer every four milliseconds.  
*
* Parameters:
*  cy_timer_callback_arg_t arg
*
* Return:
*  void
*
*******************************************************************************/
static void wdt_handler(cy_timer_callback_arg_t arg)
{
    thread_ap_watchdog_ConfigureTime(5);
}


/*******************************************************************************
* Function Name: console_task
********************************************************************************
* Summary:
* The console task does the following:
*    1. Initializes WCM and connects to configured AP
*    2. Starts a periodic software timer 
*    3. Waits for user indifinitely to input commands 
*
* Parameters:
*  cy_thread_arg_t arg
*
* Return:
*  void
*
*******************************************************************************/
static void console_task(cy_thread_arg_t arg)
{
    cy_rslt_t result;

    /* Initialize wcm */
    wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
    result = cy_wcm_init(&wcm_config);
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Wi-Fi Connection Manager initialization failed! Error code: 0x%08" PRIx32 "\n", result);
        return;
    }
    printf("Wi-Fi Connection Manager initialized.\n");

    /* Connect to an AP for which credentials are specified */
    ConnectWifi(CY_WCM_ITWT_PROFILE_NONE);

    command_console_add_command();

    /* Periodic timer is added to pet the WDT. This is required for running iperf tests without a Watchdog reset */
    cy_rtos_init_timer(&wdt_timer_t, CY_TIMER_TYPE_PERIODIC, wdt_handler, 0);
    cy_rtos_start_timer(&wdt_timer_t, WDT_TIMEOUT_MS);
    thread_ap_watchdog_ConfigureTime(5);

    while(1)
    {
        cy_rtos_delay_milliseconds(500);
    }
}


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for CM33 CPU. It does the following:
*    1. Initializes the hardware
*    2. Sets up the console task
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result ;

#if defined(H1CP_CLOCK_FREQ)
    cyhal_clock_t obj;
#endif

    /* Initialize the board support package */
    result = cybsp_init() ;

    /* Board init failed. Stop program execution */
    if(result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }
    
    /* Enable global interrupts */
    __enable_irq();

#ifdef COMPONENT_CAT5
    /* For H1CP, the BTSS sleep is enabled by default.
     * command-console will not work with sleep enabled as wake on uart is not enabled.
     * Once wake on uart is enabled, the sleep lock can be removed.
     */
    cyhal_syspm_lock_deepsleep();
#endif

#if defined(H1CP_CLOCK_FREQ)
#if (CYHAL_API_VERSION >= 2)
    /* set CPU clock to H1CP_CLOCK_FREQ */
    result = set_cpu_clock_cp(H1CP_CLOCK_FREQ, &obj);
    CY_ASSERT(result == CY_RSLT_SUCCESS) ;
#endif
#endif

    /* Initialize retarget-io to use the debug UART port */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);

    /* retarget-io init failed. Stop program execution */
    if(result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }    

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen. */
    printf("\x1b[2J\x1b[;H");

    printf("************************************************************\n"
           "                      TWT Demo Example                      \n"
           "************************************************************\n");

    result = cy_rtos_thread_create(&nxs_thread,
                                   &console_task,
                                   "ConsoleTask",
                                   &nxs_stack,
                                   THREAD_STACK,
                                   CY_RTOS_PRIORITY_LOW,
                                   0);
                                   
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    return 0;
}

#if defined(H1CP_CLOCK_FREQ)
#if (CYHAL_API_VERSION >= 2)
cy_rslt_t set_cpu_clock_cp ( uint32_t freq, cyhal_clock_t *obj )
{
    cy_rslt_t rslt;

    if (CY_RSLT_SUCCESS == (rslt = cyhal_clock_get(obj, &CYHAL_CLOCK_RSC_CPU)))
    {
        rslt = cyhal_clock_set_frequency(obj, freq, NULL);
    }

    return rslt;
}
#endif
#endif


/* [] END OF FILE */
