
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "config.h"
#include "hardware.h"
#include "serial.h"
#include "protocol.h"
#include "glcd.h"
#include "ledz.h"
#include "actuator.h"
#include "data.h"
#include "naveg.h"
#include "screen.h"
#include "cli.h"
#include "comm.h"
#include "images.h"


/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/

#define UNUSED_PARAM(var)   do { (void)(var); } while (0)
#define TASK_NAME(name)     ((const char * const) (name))
#define ACTUATOR_TYPE(act)  (((button_t *)(act))->type)

#define ACTUATORS_QUEUE_SIZE    10


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static xQueueHandle g_actuators_queue;
static uint8_t g_msg_buffer[WEBGUI_COMM_RX_BUFF_SIZE];
static uint8_t g_ui_communication_started;
static uint8_t g_protocol_bussy = 0;


/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/

// local functions
static void actuators_cb(void *actuator);

// tasks
static void procotol_task(void *pvParameters);
static void displays_task(void *pvParameters);
static void actuators_task(void *pvParameters);
static void cli_task(void *pvParameters);
static void setup_task(void *pvParameters);

// protocol callbacks
static void ping_cb(proto_t *proto);
static void say_cb(proto_t *proto);
static void led_cb(proto_t *proto);
static void glcd_text_cb(proto_t *proto);
static void glcd_dialog_cb(proto_t *proto);
static void glcd_draw_cb(proto_t *proto);
static void gui_connection_cb(proto_t *proto);
static void control_add_cb(proto_t *proto);
static void control_rm_cb(proto_t *proto);
static void control_set_cb(proto_t *proto);
static void control_get_cb(proto_t *proto);
static void initial_state_cb(proto_t *proto);
static void bank_config_cb(proto_t *proto);
static void tuner_cb(proto_t *proto);
static void resp_cb(proto_t *proto);
static void restore_cb(proto_t *proto);
static void boot_cb(proto_t *proto);

/*
************************************************************************************************************************
*           LOCAL CONFIGURATION ERRORS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*           MAIN FUNCTION
************************************************************************************************************************
*/

int main(void)
{
    // initialize hardware
    hardware_setup();

    // this task is used to setup the system and create the other tasks
    xTaskCreate(setup_task, NULL, 256, NULL, 1, NULL);

    // start the scheduler
    vTaskStartScheduler();

    // should never reach here!
    for(;;);
}


/*
************************************************************************************************************************
*           LOCAL FUNCTIONS
************************************************************************************************************************
*/

// this callback is called from UART ISR in case of error
void serial_error(uint8_t uart_id, uint32_t error)
{
    UNUSED_PARAM(uart_id);
    UNUSED_PARAM(error);
}

// this callback is called from a ISR
static void actuators_cb(void *actuator)
{
    static uint8_t i, info[ACTUATORS_QUEUE_SIZE][4];

    // does a copy of actuator id and status
    uint8_t *actuator_info;
    actuator_info = info[i];
    if (++i == ACTUATORS_QUEUE_SIZE) i = 0;

    // fills the actuator info vector
    actuator_info[0] = ((button_t *)(actuator))->type;
    actuator_info[1] = ((button_t *)(actuator))->id;
    actuator_info[2] = actuator_get_status(actuator);

    // queue actuator info
    portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
    //we want the buttons and encoders to always be handled, the pots can cluther the qeue.
    //the encoder and buttons are in this sense prioritized over the potentiometer events. 
    //for reference: https://www.freertos.org/xQueueOverwrite.html
    if ((ACTUATOR_TYPE(actuator) == BUTTON) || (ACTUATOR_TYPE(actuator) == ROTARY_ENCODER)) xQueueOverwrite(g_actuators_queue, &actuator_info);
    else xQueueSendFromISR(g_actuators_queue, &actuator_info, &xHigherPriorityTaskWoken);

    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}


/*
************************************************************************************************************************
*           TASKS
************************************************************************************************************************
*/

static void procotol_task(void *pvParameters)
{
    UNUSED_PARAM(pvParameters);

    while (1)
    {
        uint32_t msg_size;
        g_protocol_bussy = 0;
        // blocks until receive a new message
        ringbuff_t *rb = comm_webgui_read();
        msg_size = ringbuff_read_until(rb, g_msg_buffer, WEBGUI_COMM_RX_BUFF_SIZE, 0);
        // parses the message
        if (msg_size > 0)
        {
            //if parsing messages block the actuator messages. 
            g_protocol_bussy = 1;
            msg_t msg;
            msg.sender_id = 0;
            msg.data = (char *) g_msg_buffer;
            msg.data_size = msg_size;
            protocol_parse(&msg);
        }
    }
}

static void displays_task(void *pvParameters)
{
    UNUSED_PARAM(pvParameters);

    uint8_t i = 0;
    uint32_t count = 0;

    while (1)
    {
        // update GLCD
        glcd_update(hardware_glcds(i));
        if (++i == GLCD_COUNT) i = 0;

        // I know, this is embarrassing
        if (naveg_need_update())
        {
            if (++count == 500000)
            {
                naveg_update();
                count = 0;
            }
        }
        else
        {
            count = 0;
        }

        taskYIELD();
    }
}

static void actuators_task(void *pvParameters)
{
    UNUSED_PARAM(pvParameters);

    uint8_t type, id, status;
    uint8_t *actuator_info;

    while (1)
    {
        if (g_protocol_bussy) return; 

        portBASE_TYPE xStatus;

        // take the actuator from queue
        xStatus = xQueueReceive(g_actuators_queue, &actuator_info, portMAX_DELAY);

        // check if must enter in the restore mode
        if (cli_restore(RESTORE_STATUS) == NOT_LOGGED)
            cli_restore(RESTORE_CHECK_BOOT);

        // checks if actuator has successfully taken
        if (xStatus == pdTRUE && cli_restore(RESTORE_STATUS) == LOGGED_ON_SYSTEM)
        {
            type = actuator_info[0];
            id = actuator_info[1];
            status = actuator_info[2];

            // encoders
            if (type == ROTARY_ENCODER)
            {
                if (BUTTON_CLICKED(status))
                {
                    naveg_enter(id);
                }
                if (BUTTON_HOLD(status))
                {
                    id ? naveg_master_volume(id) : 
                    naveg_toggle_tool(id, id);          
                }
                if (BUTTON_RELEASED(status))
                {
                    if (id && naveg_is_master_vol()) naveg_master_volume(id);
                }
                if (ENCODER_TURNED_CW(status))
                {
                    naveg_inc_control(id);
                    naveg_down(id);
                }
                if (ENCODER_TURNED_ACW(status))
                {
                    naveg_dec_control(id);
                    naveg_up(id);
                }
                glcd_update(hardware_glcds(id));
            }

            else if (type == POT)
            {
                if (POT_TURNED(status))
                {   
                    naveg_pot_change(id);
                }
                
                glcd_update(hardware_glcds((id > 3) ? 1 : 0));
            }

            // footswitches
            else if (type == BUTTON)
            {
                if (BUTTON_PRESSED(status))
                {
                    naveg_foot_change(id, 1);
                }
                
                if (BUTTON_RELEASED(status))
                {
                    //trigger LED 
                    naveg_foot_change(id, 0);
                }

                if (BUTTON_HOLD(status))
                {
                    if (id > 3) naveg_save_page(id);
                }

                glcd_update(hardware_glcds((id > 1) ? 1 : 0));
            }
        }
    }
}

static void cli_task(void *pvParameters)
{
    UNUSED_PARAM(pvParameters);

    hardware_coreboard_power(COREBOARD_INIT);

    while (1)
    {
        cli_process();
    }
}

static void setup_task(void *pvParameters)
{
    UNUSED_PARAM(pvParameters);

    // draw start up images
    screen_image(0, mod_logo); 
    glcd_update(hardware_glcds(0));
    screen_image(1, mod_duo);
    glcd_update(hardware_glcds(1));

    // CLI initialization
    cli_init();

    // initialize the communication resources
    comm_init();

    // create the queues
    g_actuators_queue = xQueueCreate(ACTUATORS_QUEUE_SIZE, sizeof(uint8_t *));

    // create the tasks
    xTaskCreate(procotol_task, TASK_NAME("pro"), 512, NULL, 4, NULL);
    xTaskCreate(actuators_task, TASK_NAME("act"), 256, NULL, 3, NULL);
    xTaskCreate(cli_task, TASK_NAME("cli"), 128, NULL, 2, NULL);
    xTaskCreate(displays_task, TASK_NAME("disp"), 128, NULL, 1, NULL);

    // actuators callbacks
    uint8_t i;
    for (i = 0; i < SLOTS_COUNT; i++)
    {
        actuator_set_event(hardware_actuators(ENCODER0 + i), actuators_cb);
        actuator_enable_event(hardware_actuators(ENCODER0 + i), EV_ALL_ENCODER_EVENTS);
    }
    for (i = 0; i < FOOTSWITCHES_COUNT; i++)
    {
        actuator_set_event(hardware_actuators(FOOTSWITCH0 + i), actuators_cb);
        actuator_enable_event(hardware_actuators(FOOTSWITCH0 + i), EV_ALL_BUTTON_EVENTS);
    }
    for (i = 0; i < POTS_COUNT; i++)
    {
        actuator_set_event(hardware_actuators(POT0 + i), actuators_cb);
        actuator_enable_event(hardware_actuators(POT0 + i), EV_POT_TURNED);
    }
    
    // protocol definitions
    protocol_add_command(PING_CMD, ping_cb);
    protocol_add_command(SAY_CMD, say_cb);
    protocol_add_command(LED_CMD, led_cb);
    protocol_add_command(GLCD_TEXT_CMD, glcd_text_cb);
    protocol_add_command(GLCD_DIALOG_CMD, glcd_dialog_cb);
    protocol_add_command(GLCD_DRAW_CMD, glcd_draw_cb);
    protocol_add_command(GUI_CONNECTED_CMD, gui_connection_cb);
    protocol_add_command(GUI_DISCONNECTED_CMD, gui_connection_cb);
    protocol_add_command(CONTROL_ADD_CMD, control_add_cb);
    protocol_add_command(CONTROL_REMOVE_CMD, control_rm_cb);
    protocol_add_command(CONTROL_SET_CMD, control_set_cb);
    protocol_add_command(CONTROL_GET_CMD, control_get_cb);
    protocol_add_command(INITIAL_STATE_CMD, initial_state_cb);
    protocol_add_command(BANK_CONFIG_CMD, bank_config_cb);
    protocol_add_command(TUNER_CMD, tuner_cb);
    protocol_add_command(RESPONSE_CMD, resp_cb);
    protocol_add_command(RESTORE_CMD, restore_cb);
    protocol_add_command(BOOT_HMI_CMD, boot_cb);

    // init the navigation
    naveg_init();

    // deletes itself
    vTaskDelete(NULL);
}


/*
************************************************************************************************************************
*           PROTOCOL CALLBACKS
************************************************************************************************************************
*/

static void ping_cb(proto_t *proto)
{
    g_ui_communication_started = 1;
    protocol_response("resp 0", proto);
}

static void say_cb(proto_t *proto)
{
    protocol_response(proto->list[1], proto);
}

static void led_cb(proto_t *proto)
{
    ledz_t *led = hardware_leds(atoi(proto->list[1]));

    ledz_set(led, RED,   atoi(proto->list[2]));
    ledz_set(led, GREEN, atoi(proto->list[3]));
    ledz_set(led, BLUE,  atoi(proto->list[4]));

    if (proto->list_count == 7)
    ledz_blink(led, WHITE, atoi(proto->list[5]), atoi(proto->list[6]), LED_BLINK_INFINIT);

    protocol_response("resp 0", proto);
}

static void glcd_text_cb(proto_t *proto)
{
    uint8_t glcd_id, x, y;
    glcd_id = atoi(proto->list[1]);
    x = atoi(proto->list[2]);
    y = atoi(proto->list[3]);

    if (glcd_id >= GLCD_COUNT) return;

    glcd_text(hardware_glcds(glcd_id), x, y, proto->list[4], NULL, GLCD_BLACK);
    protocol_response("resp 0", proto);
}

static void glcd_dialog_cb(proto_t *proto)
{
    uint8_t val = naveg_dialog(proto->list[1]);
    char resp[16];
    strcpy(resp, "resp 0 ");
    resp[7] = val + '0';
    resp[8] = 0;
    protocol_response(resp, proto);
}

static void glcd_draw_cb(proto_t *proto)
{
    uint8_t glcd_id, x, y;
    glcd_id = atoi(proto->list[1]);
    x = atoi(proto->list[2]);
    y = atoi(proto->list[3]);

    if (glcd_id >= GLCD_COUNT) return;

    str_to_hex(proto->list[4], g_msg_buffer, sizeof(g_msg_buffer));
    glcd_draw_image(hardware_glcds(glcd_id), x, y, g_msg_buffer, GLCD_BLACK);

    protocol_response("resp 0", proto);
}

static void gui_connection_cb(proto_t *proto)
{
    if (strcmp(proto->list[0], GUI_CONNECTED_CMD) == 0)
        naveg_ui_connection(UI_CONNECTED);
    else
        naveg_ui_connection(UI_DISCONNECTED);

    protocol_response("resp 0", proto);
}

static void control_add_cb(proto_t *proto)
{
    control_t *control = data_parse_control(proto->list);
    naveg_add_control(control);

    protocol_response("resp 0", proto);
}

static void control_rm_cb(proto_t *proto)
{
    g_ui_communication_started = 1;
    
    naveg_remove_control(atoi(proto->list[1]));

    uint8_t i;
    for (i = 2; i < TOTAL_ACTUATORS + 1; i++)
    {
        if (atoi(proto->list[i]) != 0)
        {
            naveg_remove_control(atoi(proto->list[i]));
        }
        else break;
    }
    
    protocol_response("resp 0", proto);
}

static void control_set_cb(proto_t *proto)
{
    naveg_set_control(atoi(proto->list[1]), atof(proto->list[2]));
    protocol_response("resp 0", proto);
}

static void control_get_cb(proto_t *proto)
{
    float value;
    value = naveg_get_control(atoi(proto->list[1]));

    char resp[32];
    strcpy(resp, "resp 0 ");

    float_to_str(value, &resp[strlen(resp)], 8, 3);
    protocol_response(resp, proto);
}

static void initial_state_cb(proto_t *proto)
{
    g_ui_communication_started = 1;
    naveg_initial_state(proto->list[1], proto->list[2], &(proto->list[3]));
    protocol_response("resp 0", proto);
}

static void bank_config_cb(proto_t *proto)
{
    bank_config_t bank_func;
    bank_func.hw_id = atoi(proto->list[1]);
    bank_func.function = atoi(proto->list[2]);
    naveg_bank_config(&bank_func);
    protocol_response("resp 0", proto);
}

static void tuner_cb(proto_t *proto)
{
    screen_tuner(atof(proto->list[1]), proto->list[2], atoi(proto->list[3]));
    protocol_response("resp 0", proto);
}

static void resp_cb(proto_t *proto)
{
    comm_webgui_response_cb(proto->list);
}

static void restore_cb(proto_t *proto)
{
    cli_restore(RESTORE_INIT);
    protocol_response("resp 0", proto);
}

static void boot_cb(proto_t *proto)
{
    ledz_on(hardware_leds(5), PAGES1_COLOR);
    naveg_print_pb_name(DISPLAY_LEFT);
    naveg_master_volume(0);
    protocol_response("resp 0", proto);
}

/*
************************************************************************************************************************
*           ERRORS CALLBACKS
************************************************************************************************************************
*/

// TODO: better error feedback for below functions

void HardFault_Handler(void)
{
    ledz_on(hardware_leds(0), CYAN);
    while (1);
}

void MemManage_Handler(void)
{
    ledz_on(hardware_leds(1), CYAN);
    while (1);
}

void BusFault_Handler(void)
{
    ledz_on(hardware_leds(2), CYAN);
    while (1);
}

void UsageFault_Handler(void)
{
    ledz_on(hardware_leds(3), CYAN);
    while (1);
}

void vApplicationMallocFailedHook(void)
{
    ledz_on(hardware_leds(4), CYAN);
    while (1);
}

void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed portCHAR *pcTaskName)
{
    UNUSED_PARAM(pxTask);
    glcd_t *glcd0 =  hardware_glcds(0);
    glcd_clear(glcd0, GLCD_WHITE);
    glcd_text(glcd0, 0, 0, "stack overflow", NULL, GLCD_BLACK);
    glcd_text(glcd0, 0, 10, (const char *) pcTaskName, NULL, GLCD_BLACK);
    glcd_update(glcd0);
    ledz_on(hardware_leds(5), CYAN);
    while (1);
}
