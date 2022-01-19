
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "system.h"
#include "config.h"
#include "data.h"
#include "naveg.h"
#include "hardware.h"
#include "actuator.h"
#include "ui_comm.h"
#include "sys_comm.h"
#include "cli.h"
#include "screen.h"
#include "glcd_widget.h"
#include "glcd.h"
#include "utils.h"
#include "device.h"
#include "calibration.h"
#include "uc1701.h"
#include "mod-protocol.h"

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

// systemctl services names
const char *systemctl_services[] = {
    "jack2",
    "sshd",
    "mod-ui",
    "dnsmasq",
    NULL
};

const char *versions_names[] = {
    "version",
    "restore",
    "system",
    "controller",
    NULL
};

char *option_enabled = "[X]";
char *option_disabled = "[ ]";

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
#define ROUND(x)    ((x) > 0.0 ? (((float)(x)) + 0.5) : (((float)(x)) - 0.5))
#define MAP(x, Omin, Omax, Nmin, Nmax)      ( x - Omin ) * (Nmax -  Nmin)  / (Omax - Omin) + Nmin;


/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

float g_gains_volumes[5] = {-1, -1, -1, -1, -1};
uint8_t g_q_bypass = 0;
uint8_t g_bypass[4] = {};
uint8_t g_current_profile = 1;
uint8_t g_quick_bypass_channel = 0;
uint8_t g_sl_out = 0;
uint8_t g_sl_in = 0;
uint8_t g_snapshot_prog_change = 0;
uint8_t g_pedalboard_prog_change = 0;
uint16_t g_beats_per_minute = 0;
uint8_t g_beats_per_bar = 0;
uint8_t g_MIDI_clk_send = 0;
uint8_t g_MIDI_clk_src = 0;
uint8_t g_play_status = 0;
uint8_t g_tuner_mute = 0;
int8_t g_display_brightness = -1;
int8_t g_actuator_hide = -1;
int8_t g_pots_lock = -1;
int8_t g_cv_in_mode = -1;
int8_t g_exp_mode = -1;
int8_t g_cv_out_mode = -1;
int16_t g_display_contrast_left = -1;
int16_t g_display_contrast_right = -1;
int8_t g_page_mode = -1;
int8_t g_led_brightness = -1;

/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/


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

static void update_status(char *item_to_update, const char *response)
{
    if (!item_to_update) return;

    char *pstr = strstr(item_to_update, ":");
    if (pstr && response)
    {
        pstr++;
        *pstr++ = ' ';
        strcpy(pstr, response);
    }
}


void add_chars_to_menu_name(menu_item_t *item, char *chars_to_add)
{
        //if no good data
        if ((!chars_to_add)||(!item)) return; 

        //always copy the clean name
        strcpy(item->name, item->desc->name);
        uint8_t value_size = strlen(chars_to_add);
        uint8_t name_size = strlen(item->name);
        uint8_t q;
        //add spaces until so we allign the chars_to_add to the left
        for (q = 0; q < (MENU_LINE_CHARS - name_size - value_size); q++)
        {
            strcat(item->name, " ");
        }

        strcat(item->name, chars_to_add);
}

//TODO CHECK IF WE CAN USE DYNAMIC MEMORY HERE
void set_item_value(char *command, uint16_t value)
{
    uint8_t i;
    char buffer[50];

    i = copy_command((char *)buffer, command);

    // copy the value
    char str_buf[8];
    int_to_str(value, str_buf, 4, 0);
    const char *p = str_buf;
    while (*p)
    {
        buffer[i++] = *p;
        p++;
    }
    buffer[i] = 0;

    // sets the response callback
    ui_comm_webgui_set_response_cb(NULL, NULL);

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);
}

static void set_menu_item_value(uint16_t menu_id, uint16_t value)
{
    uint8_t i = 0;
    char buffer[50];
    memset(buffer, 0, sizeof buffer);

    i = copy_command((char *)buffer, CMD_MENU_ITEM_CHANGE);

    // copy the id
    i += int_to_str(menu_id, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    // copy the value
    i += int_to_str(value, &buffer[i], sizeof(buffer) - i, 0);

    buffer[i++] = 0;

    // sets the response callback
    ui_comm_webgui_set_response_cb(NULL, NULL);

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);
}

static void volume(menu_item_t *item, int event, const char *source, float min, float max, float step)
{
    static uint32_t last_message_time = 0; 
    char value[8] = {};
    static const char *response = NULL;
    cli_command(NULL, CLI_DISCARD_RESPONSE);
    uint8_t dir = (source[0] == 'i') ? 0 : 1;

    uint32_t message_time = hardware_timestamp();

    if (((event == MENU_EV_UP) || (event == MENU_EV_DOWN)) && (dir ? g_sl_out : g_sl_in) && (item->desc->id != HP_VOLUME))
    {
        //change volume for both
        //PGA (input)
        if (!dir)
        {
            if (message_time - last_message_time > VOL_MESSAGE_TIMEOUT)
            {
                int_to_str(item->data.value, value, 8, 0);
                cli_command("mod-amixer in 0 xvol ", CLI_CACHE_ONLY);
                cli_command(value, CLI_DISCARD_RESPONSE);

                last_message_time = message_time;
            }
        }
        //DAC (output)
        else
        {
            if (message_time - last_message_time > VOL_MESSAGE_TIMEOUT)
            {
                int_to_str(item->data.value, value, 8, 0);
                cli_command("mod-amixer out 0 xvol ", CLI_CACHE_ONLY);
                cli_command(value, CLI_DISCARD_RESPONSE);

                last_message_time = message_time;
            }
        }
    }
    else
    {
    	if ((event == MENU_EV_ENTER) || (event == MENU_EV_NONE))
    	{
    		char str[LINE_BUFFER_SIZE+1];

            if (event == MENU_EV_ENTER)
            {
                cli_command("mod-amixer ", CLI_CACHE_ONLY);
                cli_command(source, CLI_CACHE_ONLY);
                cli_command(" xvol", CLI_CACHE_ONLY);
                response = cli_command(NULL, CLI_RETRIEVE_RESPONSE);
                strcpy(str, response);
                item->data.value = atof(str);
            }
            else
            {
                item->data.value = g_gains_volumes[item->desc->id - VOLUME_ID];
            }

            item->data.min = min;
            item->data.max = max;
            item->data.step = step;
        }
        else if ((event == MENU_EV_UP) ||(event == MENU_EV_DOWN))
        {
            float_to_str(item->data.value, value, 8, 1);
            cli_command("mod-amixer ", CLI_CACHE_ONLY);
            cli_command(source, CLI_CACHE_ONLY);
            cli_command(" xvol ", CLI_CACHE_ONLY);
            cli_command(value, CLI_DISCARD_RESPONSE);
        }
    }

    //save gains globaly for stereo link functions
    g_gains_volumes[item->desc->id - VOLUME_ID] = item->data.value;

    char str_bfr[10] = {};

    //input
    if (!dir) {
        float value_bfr;
        value_bfr = MAP(item->data.value, min, max, -12, 25);
        float_to_str(value_bfr, str_bfr, 8, 2);
    }
    //output, headphones
    else {
        float_to_str(item->data.value, str_bfr, 8, 2);
    }

    strcpy(item->name, item->desc->name);
    uint8_t q;
    uint8_t value_size = strlen(str_bfr);
    uint8_t name_size = strlen(item->name);
    for (q = 0; q < (31 - name_size - value_size - 3); q++)
    {
        strcat(item->name, " ");
    }
    strcat(item->name, str_bfr);
    strcat(item->name, " dB");

    //if stereo link is on we need to update the other menu item as well
    if ((((event == MENU_EV_UP) || (event == MENU_EV_DOWN)) && (dir ? g_sl_out : g_sl_in))&& (item->desc->id != HP_VOLUME))
    {
        if (strchr(source, '1'))
            naveg_update_gain(DISPLAY_RIGHT, item->desc->id + 1, item->data.value, min, max);
        else
            naveg_update_gain(DISPLAY_RIGHT, item->desc->id - 1, item->data.value, min, max);
    }
    
    naveg_settings_refresh(DISPLAY_RIGHT);
}

/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

uint8_t system_get_current_profile(void)
{
    return g_current_profile;
}

//I'm not a system callback
float system_master_volume_cb(float value, int event)
{
   /* //what is the master volume currently connected to? and convert it to a char
    char channel_char[8];
    int_to_str(g_master_vol_port, channel_char, 4, 0);

    if ((event == MENU_EV_ENTER) || (event == MENU_EV_NONE))
    {
        //get the value
        cli_command("mod-amixer out ", CLI_CACHE_ONLY);
        cli_command(channel_char, CLI_CACHE_ONLY);
        cli_command(" xvol ", CLI_CACHE_ONLY);

        //convert and return
        const char *response = cli_command(NULL, CLI_RETRIEVE_RESPONSE);
        return atof(response);
    }
    //chaning the master volume
    else if ((event == MENU_EV_UP) ||(event == MENU_EV_DOWN))
    {
        char value_char[8];

        //set the value
        int_to_str(value, value_char, 8, 0);
        cli_command("mod-amixer out ", CLI_CACHE_ONLY);
        cli_command(channel_char, CLI_CACHE_ONLY);
        cli_command(" xvol ", CLI_CACHE_ONLY);
        cli_command(value_char, CLI_DISCARD_RESPONSE);
        
        return value;
    }*/
    //ERROR
    return 0;
}

//I SHOULD NOT BE HERE
void system_save_gains_cb(void *arg, int event)
{
    UNUSED_PARAM(arg);

    if (event == MENU_EV_ENTER)
    {
        cli_command("mod-amixer save", CLI_DISCARD_RESPONSE);
    }
}

void system_update_menu_value(uint8_t item_ID, uint16_t value)
{
    switch(item_ID)
    {
        //play status
        case MENU_ID_PLAY_STATUS:
            g_play_status = value;
        break;
        //global tempo
        case MENU_ID_TEMPO:
            g_beats_per_minute = value;
        break;
        //global tempo status
        case MENU_ID_BEATS_PER_BAR:
            g_beats_per_bar = value;
        break;
        //tuner mute
        case MENU_ID_TUNER_MUTE: 
            g_tuner_mute = value;
        break;
        //bypass channel 1
        case MENU_ID_BYPASS1: 
            g_bypass[0] = value;
        break;
        //bypass channel 2
        case MENU_ID_BYPASS2: 
            g_bypass[1] = value;
        break;
        //quick bypass channel
        case MENU_ID_QUICK_BYPASS: 
            g_q_bypass = value;
        break;
        //sl input
        case MENU_ID_SL_IN: 
            g_sl_in = value;
        break;
        //stereo link output
        case MENU_ID_SL_OUT: 
            g_sl_out = value;
        break;
        //MIDI clock source
        case MENU_ID_MIDI_CLK_SOURCE: 
            g_MIDI_clk_src = value;
        break;
        //send midi clock
        case MENU_ID_MIDI_CLK_SEND: 
            g_MIDI_clk_send = value;
        break;
        //snapshot prog change 
        case MENU_ID_SNAPSHOT_PRGCHGE: 
            g_snapshot_prog_change = value;
        break;
        //pedalboard prog change 
        case MENU_ID_PB_PRGCHNGE: 
            g_pedalboard_prog_change = value;
        break;
        //user profile change 
        case MENU_ID_CURRENT_PROFILE: 
            g_current_profile = value;
        break;
        //display brightness
        case MENU_ID_BRIGHTNESS: 
            g_display_brightness = value;
            hardware_glcd_brightness(g_display_brightness); 
        break;
        //CV in mode
        case MENU_ID_EXP_CV_INPUT: 
            g_cv_in_mode = value;
        break;
        //expression mode
        case MENU_ID_EXP_MODE: 
            g_exp_mode = value;
        break;
        //CV out mode
        case MENU_ID_HP_CV_OUTPUT: 
            g_cv_out_mode = value;
        break;
        default:
            return;
        break;
    }
}
/*
************************************************************************************************************************
*           MENU SYSTEM FUNCTIONS (Called by their callback defined in config.h)
************************************************************************************************************************
*/

void system_pedalboard_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER && item->data.hover == 0)
    {
        switch (item->desc->id)
        {
            case PEDALBOARD_SAVE_ID:
                ui_comm_webgui_send(CMD_PEDALBOARD_SAVE, strlen(CMD_PEDALBOARD_SAVE));
                break;

            case PEDALBOARD_RESET_ID:
                ui_comm_webgui_send(CMD_PEDALBOARD_RESET, strlen(CMD_PEDALBOARD_RESET));
                break;
        }
    }
}

void system_bluetooth_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        const char *response;
        char resp[LINE_BUFFER_SIZE];
        if (item->desc->id == BLUETOOTH_ID)
        {
            response = cli_command("mod-bluetooth hmi", CLI_RETRIEVE_RESPONSE);
            
            strncpy(resp, response, sizeof(resp)-1);
            char **items = strarr_split(resp, '|');;

            if (items)
            {
                update_status(item->data.list[2], items[0]);
                update_status(item->data.list[3], items[1]);
                update_status(item->data.list[4], items[2]);
                FREE(items);
            } 
        }
        else if (item->desc->id == BLUETOOTH_DISCO_ID)
        {
            cli_command("mod-bluetooth discovery", CLI_DISCARD_RESPONSE);
        }
    }
}

void system_input_cb(void *arg, int event)
{
    (void) arg;

    if (event == MENU_EV_ENTER)
    {
        const char *response;
        char resp[LINE_BUFFER_SIZE];

        response = cli_command("mod-amixer in xall", CLI_RETRIEVE_RESPONSE);
        strncpy(resp, response, sizeof(resp)-1);
        char **items = strarr_split(resp, '|');;

        if (items)
        {
            g_gains_volumes[0] = atof(items[0]);
            g_gains_volumes[1] = atof(items[1]);
            g_cv_in_mode = atoi(items[2]);
            g_exp_mode = atoi(items[3]);
            FREE(items);
        } 
        
    }
}

void system_output_cb(void *arg, int event)
{
    (void) arg;
    
    if (event == MENU_EV_ENTER)
    {
        const char *response;
        char resp[LINE_BUFFER_SIZE];

        response = cli_command("mod-amixer out xall", CLI_RETRIEVE_RESPONSE);
           
        strncpy(resp, response, sizeof(resp)-1);
        char **items = strarr_split(resp, '|');;

        if (items)
        {
            g_gains_volumes[2] = atof(items[0]);
            g_gains_volumes[3] = atof(items[1]);
            g_gains_volumes[4] = atof(items[2]);
            g_cv_out_mode = atoi(items[3]);
            FREE(items);
        } 
    }
}

void system_services_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        uint8_t i = 0;
        while (systemctl_services[i])
        {
            const char *response;
            response = cli_systemctl("is-active ", systemctl_services[i]);
            update_status(item->data.list[i+1], response);
            i++;
        }
    }
}

void system_versions_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        const char *response;
        char version[8];

        uint8_t i = 0;
        while (versions_names[i])
        {
            cli_command("mod-version ", CLI_CACHE_ONLY);
            response = cli_command(versions_names[i], CLI_RETRIEVE_RESPONSE);
            strncpy(version, response, (sizeof version) - 1);
            version[(sizeof version) - 1] = 0;
            update_status(item->data.list[i+1], version);
            screen_system_menu(item);
            i++;
        }
    }
}

void system_release_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        const char *response;
        response = cli_command("mod-version release", CLI_RETRIEVE_RESPONSE);
        item->data.popup_content = response;
    }
}

void system_tag_cb(void *arg, int event)
{

    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        const char *response;
        char *txt = "The serial number of your     device is:                    ";
        response =  cli_command("cat /var/cache/mod/tag", CLI_RETRIEVE_RESPONSE);
        char * bfr = (char *) MALLOC(1 + strlen(txt)+ strlen(response));
        strcpy(bfr, txt);
        strcat(bfr, response);
        item->data.popup_content = bfr;
        item->data.popup_header = "serial number";
    }
}

void system_upgrade_cb(void *arg, int event)
{
    if (event == MENU_EV_ENTER)
    {
        menu_item_t *item = arg;
        button_t *foot = (button_t *) hardware_actuators(FOOTSWITCH0);

        // check if YES option was chosen
        if (item->data.hover == 0)
        {
            uint8_t status = actuator_get_status(foot);

            // check if footswitch is pressed down
            if (BUTTON_PRESSED(status))
            {
                //clear all screens
                screen_clear(DISPLAY_LEFT);
                screen_clear(DISPLAY_RIGHT);

                // start restore
                cli_restore(RESTORE_INIT);
            }
        }
    }
}

void system_volume_cb(void *arg, int event)
{
    menu_item_t *item = arg;
    float min, max, step;
    const char *source;

        switch (item->desc->id)
        {
            case IN1_VOLUME:
                source = "in 1";
                min = 0;
                max = 74.0;
                step = 1;
                break;

            case IN2_VOLUME:
                source = "in 2";
                min = 0;
                max = 74.0;
                step = 1;
                break;

            case OUT1_VOLUME:
                source = "out 1";
                min = -99.5;
                max = 0.0;
                step = 2.0;
                break;

            case OUT2_VOLUME:
                source = "out 2";
                min = -99.5;
                max = 0.0;
                step = 2.0;
                break;

            case HP_VOLUME:
                source = "hp";
                min = -33.0;
                max = 12.0;
                step = 3.0;
                break;
            default:
                return;
                break;
        }

        volume(item, event, source, min, max, step);
}

void system_banks_cb(void *arg, int event)
{
    UNUSED_PARAM(arg);

    if (event == MENU_EV_ENTER)
    {
        naveg_toggle_tool(DISPLAY_TOOL_NAVIG, 1);
    }
}

void system_display_cb(void *arg, int event)
{
    if (g_display_brightness == -1)
    {
        //read EEPROM
        uint8_t read_buffer = 0;
        EEPROM_Read(0, DISPLAY_BRIGHTNESS_ADRESS, &read_buffer, MODE_8_BIT, 1);

        g_display_brightness = read_buffer;

        hardware_glcd_brightness(g_display_brightness); 
    }

    if (event == MENU_EV_ENTER)
    {
        if (g_display_brightness < MAX_BRIGHTNESS) g_display_brightness++;
        else g_display_brightness = 0;
        
        //also write to EEPROM
        uint8_t write_buffer = g_display_brightness;
        EEPROM_Write(0, DISPLAY_BRIGHTNESS_ADRESS, &write_buffer, MODE_8_BIT, 1);

        hardware_glcd_brightness(g_display_brightness); 
    }

    if (arg != NULL)
    {
        menu_item_t *item = arg;
        char str_bfr[8];
        int_to_str((g_display_brightness * 25), str_bfr, 4, 0);
        strcat(str_bfr, "%");
        add_chars_to_menu_name(item, str_bfr);
    }
    
    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);    
}

void system_display_contrast_lcb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (g_display_contrast_left == -1)
    {
        //read EEPROM
        uint8_t read_buffer = 0;
        EEPROM_Read(0, DISPLAY_CONTRAST_LEFT_ADRESS, &read_buffer, MODE_8_BIT, 1);

        g_display_contrast_left = read_buffer;
    }

    if (event == MENU_EV_ENTER)
    {
        //save to eeprom
        if (g_display_contrast_left != -1)
        {
            uint8_t write_buffer = g_display_contrast_left;
            EEPROM_Write(0, DISPLAY_CONTRAST_LEFT_ADRESS, &write_buffer, MODE_8_BIT, 1);
        }
    }
    else if (event == MENU_EV_NONE)
    {
        //only display value
        item->data.value = g_display_contrast_left;
        item->data.min = UC1701_PM_MIN;
        item->data.max = UC1701_PM_MAX;
        item->data.step = 1;
    }
    else 
    {
        g_display_contrast_left = item->data.value;

        //write to display
        uc1701_set_custom_value(hardware_glcds(0), g_display_contrast_left, UC1701_RR_DEFAULT);
    }

    char str_bfr[8];
    int value_bfr = MAP(g_display_contrast_left, UC1701_PM_MIN, UC1701_PM_MAX, 0, 100);
    int_to_str(value_bfr, str_bfr, 5, 0);
    strcat(str_bfr, "%");
    add_chars_to_menu_name(item, str_bfr);
}

void system_display_contrast_rcb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (g_display_contrast_right == -1)
    {
        //read EEPROM
        uint8_t read_buffer = 0;
        EEPROM_Read(0, DISPLAY_CONTRAST_RIGHT_ADRESS, &read_buffer, MODE_8_BIT, 1);

        g_display_contrast_right = read_buffer;
    }

    if (event == MENU_EV_ENTER)
    {
        //save to eeprom
        if (g_display_contrast_right != -1)
        {
            uint8_t write_buffer = g_display_contrast_right;
            EEPROM_Write(0, DISPLAY_CONTRAST_RIGHT_ADRESS, &write_buffer, MODE_8_BIT, 1);
        }
    }
    else if (event == MENU_EV_NONE)
    {
        //only display value
        item->data.value = g_display_contrast_right;
        item->data.min = UC1701_PM_MIN;
        item->data.max = UC1701_PM_MAX;
        item->data.step = 1;
    }
    else
    {
        g_display_contrast_right = item->data.value;

        //write to display
        uc1701_set_custom_value(hardware_glcds(1), g_display_contrast_right, UC1701_RR_DEFAULT);
    }

    char str_bfr[8];
    int value_bfr = MAP(g_display_contrast_right, UC1701_PM_MIN, UC1701_PM_MAX, 0, 100);
    int_to_str(value_bfr, str_bfr, 5, 0);
    strcat(str_bfr, "%");
    add_chars_to_menu_name(item, str_bfr);
}

void system_hide_actuator_cb(void *arg, int event)
{
    if (g_actuator_hide == -1)
    {
        //read EEPROM
        uint8_t read_buffer = 0;
        EEPROM_Read(0, HIDE_ACTUATOR_ADRESS, &read_buffer, MODE_8_BIT, 1);

        g_actuator_hide = read_buffer;

        //write to screen.c
        screen_set_hide_non_assigned_actuators(g_actuator_hide);
    }

    if (event == MENU_EV_ENTER)
    {
        if (g_actuator_hide == 0) g_actuator_hide = 1;
        else g_actuator_hide = 0;
        
        //also write to EEPROM
        uint8_t write_buffer = g_actuator_hide;
        EEPROM_Write(0, HIDE_ACTUATOR_ADRESS, &write_buffer, MODE_8_BIT, 1);

        //write to screen.c
        screen_set_hide_non_assigned_actuators(g_actuator_hide);
    }

    if (arg != NULL)
    {
        menu_item_t *item = arg;
        add_chars_to_menu_name(item, g_actuator_hide ? "HIDE" : "SHOW");
    }

    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);    
}

void system_lock_pots_cb(void *arg, int event)
{
    if (g_pots_lock == -1)
    {
        //read EEPROM
        uint8_t read_buffer = 0;
        EEPROM_Read(0, LOCK_POTENTIOMTERS_ADRESS, &read_buffer, MODE_8_BIT, 1);

        g_pots_lock = read_buffer;

        //write to naveg.c
        naveg_lock_pots(g_pots_lock);
    }

    if (event == MENU_EV_ENTER)
    {
        if (g_pots_lock == 0) g_pots_lock = 1;
        else g_pots_lock = 0;
        
        //also write to EEPROM
        uint8_t write_buffer = g_pots_lock;
        EEPROM_Write(0, LOCK_POTENTIOMTERS_ADRESS, &write_buffer, MODE_8_BIT, 1);

        //write to naveg.c
        naveg_lock_pots(g_pots_lock);
    }

    if (arg != NULL)
    {
        menu_item_t *item = arg;
        add_chars_to_menu_name(item, g_pots_lock ? "GRAB" : "DIRECT");
    }
    
    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);    
}

void system_sl_in_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_sl_in == 0) g_sl_in = 1;
        else g_sl_in = 0;

        set_menu_item_value(MENU_ID_SL_IN, g_sl_in);

        //if we toggled to 1, we need to change gain 2 to  gain 1
        char value_bfr[8] = {};
        if (g_sl_in == 1)
        {
            float_to_str(g_gains_volumes[IN1_VOLUME - VOLUME_ID], value_bfr, 8, 1);
            cli_command("mod-amixer in 0 xvol ", CLI_CACHE_ONLY);
            cli_command(value_bfr, CLI_DISCARD_RESPONSE);
            //keep everything in sync
            g_gains_volumes[IN2_VOLUME - VOLUME_ID] = g_gains_volumes[IN1_VOLUME - VOLUME_ID];

            naveg_update_gain(DISPLAY_RIGHT, IN2_VOLUME, g_gains_volumes[IN1_VOLUME - VOLUME_ID], 0, 78);

            system_save_gains_cb(NULL, MENU_EV_ENTER);
        }
    }

    char str_bfr[4] = {};
    if (g_sl_in == 1) strcpy(str_bfr,option_enabled);
    else strcpy(str_bfr,option_disabled);   
    add_chars_to_menu_name(item, str_bfr);

    //gains can change because of this, update the menu
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);
}

void system_sl_out_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_sl_out == 0)
        {   
            g_sl_out = 1;
            
            //also set the gains to the same value
            char value_bfr[8] = {};
            float_to_str(g_gains_volumes[OUT1_VOLUME - VOLUME_ID], value_bfr, 8, 1);
            cli_command("mod-amixer out 0 xvol ", CLI_CACHE_ONLY);
            cli_command(value_bfr, CLI_DISCARD_RESPONSE);
            //keep everything in sync
            g_gains_volumes[OUT2_VOLUME - VOLUME_ID] = g_gains_volumes[OUT1_VOLUME - VOLUME_ID];

            naveg_update_gain(DISPLAY_RIGHT, OUT2_VOLUME, g_gains_volumes[OUT1_VOLUME - VOLUME_ID], 0, 78);

            system_save_gains_cb(NULL, MENU_EV_ENTER);
        }
        else {
            g_sl_out = 0;
        }
        set_menu_item_value(MENU_ID_SL_OUT, g_sl_out);
    }

    char str_bfr[4] = {};
    if (g_sl_out == 1) strcpy(str_bfr,option_enabled);
    else strcpy(str_bfr,option_disabled);   
    add_chars_to_menu_name(item, str_bfr);

    //gains can change because of this, update the whole menu
    if (event == MENU_EV_ENTER) naveg_menu_refresh(DISPLAY_RIGHT);
}

void system_tuner_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_tuner_mute == 0) g_tuner_mute= 1;
        else g_tuner_mute = 0;
        set_menu_item_value(MENU_ID_TUNER_MUTE, g_tuner_mute);
    }
    char str_bfr[15] = {};
    strcpy(str_bfr,"MUTE ");
    strcat(str_bfr,(g_tuner_mute ? option_enabled : option_disabled));
    add_chars_to_menu_name(item, str_bfr);

    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_LEFT);
}

void system_play_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_play_status == 0) g_play_status = 1;
        else g_play_status = 0;
        set_menu_item_value(MENU_ID_PLAY_STATUS, g_play_status);
    }
    char str_bfr[15] = {};
    strcpy(str_bfr,"PLAY ");
    strcat(str_bfr,(g_play_status ? option_enabled : option_disabled));
    add_chars_to_menu_name(item, str_bfr);

    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_LEFT);
}

void system_midi_src_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_MIDI_clk_src < 2) g_MIDI_clk_src++;
        else g_MIDI_clk_src = 0;
        set_menu_item_value(MENU_ID_MIDI_CLK_SOURCE, g_MIDI_clk_src);
    }

    //translate the int to string value for the menu
    char str_bfr[13] = {};
    if (g_MIDI_clk_src == 0) strcpy(str_bfr,"INTERNAL");
    else if (g_MIDI_clk_src == 1) strcpy(str_bfr,"MIDI");
    else if (g_MIDI_clk_src == 2) strcpy(str_bfr,"ABLETON LINK");

    add_chars_to_menu_name(item, str_bfr);
}

void system_midi_send_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_MIDI_clk_send == 0) g_MIDI_clk_send = 1;
        else g_MIDI_clk_send = 0;
        set_menu_item_value(MENU_ID_MIDI_CLK_SEND, g_MIDI_clk_send);
    }

    add_chars_to_menu_name(item, (g_MIDI_clk_send? option_enabled : option_disabled));
}

void system_ss_prog_change_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        set_menu_item_value(MENU_ID_SNAPSHOT_PRGCHGE, item->data.value);
    }
    else if (event == MENU_EV_NONE)
    {
        //set the item value to the snapshot_prog_change since mod-ui is master
        item->data.value = g_snapshot_prog_change;
        item->data.min = 0;
        item->data.max = 16;
        item->data.step = 1;
    }
    else 
    {
        //HMI changes the item, resync
        g_snapshot_prog_change = item->data.value;
        //let mod-ui know
        set_menu_item_value(MENU_ID_SNAPSHOT_PRGCHGE, g_snapshot_prog_change);
    }

    char str_bfr[8] = {};
    int_to_str(g_snapshot_prog_change, str_bfr, 3, 0);
    //a value of 0 means we turn off
    if (g_snapshot_prog_change == 0) strcpy(str_bfr, "OFF");
    add_chars_to_menu_name(item, str_bfr);
}

void system_pb_prog_change_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        set_menu_item_value(MENU_ID_PB_PRGCHNGE, item->data.value);
    }
    else if (event == MENU_EV_NONE)
    {
        //set the item value to the pedalboard_prog_change since mod-ui is master
        item->data.value = g_pedalboard_prog_change;
        item->data.min = 0;
        item->data.max = 16;
        item->data.step = 1;
    }
    //scrolling up/down
    else 
    {
        //HMI changes the item, resync
        g_pedalboard_prog_change = item->data.value;
        //let mod-ui know
        set_menu_item_value(MENU_ID_PB_PRGCHNGE, g_pedalboard_prog_change);
    }

    char str_bfr[8] = {};
    int_to_str(g_pedalboard_prog_change, str_bfr, 3, 0);
    //a value of 0 means we turn off
    if (g_pedalboard_prog_change == 0) strcpy(str_bfr, "OFF");
    add_chars_to_menu_name(item, str_bfr);
}

void system_tempo_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        //we can only change tempo when not linked to MIDI
        if (g_MIDI_clk_src != 1) set_menu_item_value(MENU_ID_TEMPO, item->data.value);
    }
    else if (event == MENU_EV_NONE)
    {
        //set the item value to the bpm since mod-ui is master
        item->data.value =  g_beats_per_minute;
        item->data.min = 20;
        item->data.max = 280;
        item->data.step = 1;
    }
    //scrolling up/down
    else 
    {
        //we can only change tempo when not linked to MIDI
        if (g_MIDI_clk_src != 1)
        {
            //HMI changes the item, resync
            g_beats_per_minute = item->data.value;
            //let mod-ui know
            set_menu_item_value(MENU_ID_TEMPO, g_beats_per_minute);
        }
        else 
        {
            item->data.value = g_beats_per_minute;
        }
    }

    char str_bfr[8] = {};
    int_to_str(g_beats_per_minute, str_bfr, 4, 0);
    strcat(str_bfr, " BPM");
    add_chars_to_menu_name(item, str_bfr);
}

void system_bpb_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        set_menu_item_value(MENU_ID_BEATS_PER_BAR, item->data.value);
    }
    else if (event == MENU_EV_NONE)
    {
        //set the item value to the bpb since mod-ui is master
        item->data.value =  g_beats_per_bar;
        item->data.min = 1;
        item->data.max = 16;
        item->data.step = 1;
    }
    //scrolling up/down
    else 
    {
        //HMI changes the item, resync
        g_beats_per_bar = item->data.value;
        //let mod-ui know
        set_menu_item_value(MENU_ID_BEATS_PER_BAR, g_beats_per_bar);
    }

    //add the items to the 
    char str_bfr[8] = {};
    int_to_str(g_beats_per_bar, str_bfr, 4, 0);
    strcat(str_bfr, "/4");
    add_chars_to_menu_name(item, str_bfr);
}

void system_bypass_cb (void *arg, int event)
{
    menu_item_t *item = arg; 

    //0=in1, 1=in2, 2=in1&2
    switch (item->desc->id)
    {
        //in 1
        case BP1_ID:
            //we need to toggle the bypass
            if (event == MENU_EV_ENTER)
            {
                //we toggle the bypass 
                g_bypass[0] = !g_bypass[0];
                set_menu_item_value(MENU_ID_BYPASS1, g_bypass[0]);
            }
            add_chars_to_menu_name(item, (g_bypass[0]? option_enabled : option_disabled));
        break;

        //in2
        case BP2_ID:
            //we need to toggle the bypass
            if (event == MENU_EV_ENTER)
            {
                //we toggle the bypass 
                g_bypass[1] = !g_bypass[1];
                set_menu_item_value(MENU_ID_BYPASS2, g_bypass[1]);
            }
            add_chars_to_menu_name(item, (g_bypass[1]? option_enabled : option_disabled));
        break;

        case BP12_ID:
            if (event == MENU_EV_ENTER)
            {
                //toggle the bypasses
                g_bypass[2] = !g_bypass[2];
                set_menu_item_value(MENU_ID_BYPASS1, g_bypass[2]);
                set_menu_item_value(MENU_ID_BYPASS2, g_bypass[2]);
                g_bypass[0] = g_bypass[2];
                g_bypass[1] = g_bypass[2];
            }
            add_chars_to_menu_name(item, (g_bypass[2]? option_enabled : option_disabled));
        break;
    }

    //if both are on after a change we need to change bypass 1&2 as well
    if (g_bypass[0] && g_bypass[1])
    {
        g_bypass[2] = 1;
    }
    else g_bypass[2] = 0;

    //this setting changes just 1 item on the left screen but we need to update the item first
    //naveg_settings_refresh(DISPLAY_LEFT);

    //other items can change because of this, update the whole menu on the right, and left because of the quick bypass
    if (event == MENU_EV_ENTER)
    {
        naveg_menu_refresh(DISPLAY_LEFT);
        naveg_menu_refresh(DISPLAY_RIGHT);
    }
}

void system_qbp_channel_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        //count from 0 to 2 
        if (g_q_bypass < 2) g_q_bypass++;
        else g_q_bypass = 0;
        set_menu_item_value(MENU_ID_QUICK_BYPASS, g_q_bypass);
    }
    
    //get the right char to put on the screen
    char channel_value[4];
    switch (g_q_bypass)
    {
        case 0:
                strcpy(channel_value, "  1");
            break;
        case 1:
                strcpy(channel_value, "  2");
            break;
        case 2:
                strcpy(channel_value, "1&2");
            break;
    }
    add_chars_to_menu_name(item, channel_value);

    //this setting changes just 1 item on the left screen, though it needs to be added to its node, we need to cycle through
    if (event == MENU_EV_ENTER)naveg_menu_refresh(DISPLAY_LEFT);

    //this setting changes just 1 item on the right screen
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);
}

void system_quick_bypass_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    char str_bfr[15] = {};

    //bypass[0] = in1, bypass[1] = in2
    switch(g_q_bypass)
    {
        //bypass 1
        case (0):
            if (event == MENU_EV_ENTER)
            {
                //we toggle the bypass 
                g_bypass[0] = !g_bypass[0];
                set_menu_item_value(MENU_ID_BYPASS1, g_bypass[0]);
            }
            strcpy(str_bfr,"BYPASS ");
            strcat(str_bfr, (g_bypass[0]? option_enabled : option_disabled));
            add_chars_to_menu_name(item, str_bfr);
        break;
        //bypass 2
        case (1):
            if (event == MENU_EV_ENTER)
            {
                //we toggle the bypass 
                g_bypass[1] = !g_bypass[1];
                set_menu_item_value(MENU_ID_BYPASS2, g_bypass[1]);
            }
            strcpy(str_bfr,"BYPASS ");
            strcat(str_bfr, (g_bypass[1]? option_enabled : option_disabled));
            add_chars_to_menu_name(item, str_bfr);
        break;
        //bypass 1&2
        case (2):
            if (event == MENU_EV_ENTER)
            {
                //we toggle the bypass
                g_bypass[2] = !g_bypass[2];
                set_menu_item_value(MENU_ID_BYPASS1, g_bypass[2]);
                set_menu_item_value(MENU_ID_BYPASS2, g_bypass[2]);
                g_bypass[0] = g_bypass[2];
                g_bypass[1] = g_bypass[2];
            }
            strcpy(str_bfr,"BYPASS ");
            strcat(str_bfr, (g_bypass[2]? option_enabled : option_disabled));
            add_chars_to_menu_name(item, str_bfr);
        break;
    }

    //if both are on after a change we need to change bypass 1&2 as well
    if (g_bypass[0] && g_bypass[1])
    {
        g_bypass[2] = 1;
    }
    else g_bypass[2] = 0;

     //other items can change because of this, update the whole menu on the right, and left because of the quick bypass
    if (event == MENU_EV_ENTER)
    {
        naveg_settings_refresh(DISPLAY_LEFT);
        naveg_menu_refresh(DISPLAY_RIGHT);
    }
}

//USER PROFILE X (loading)
void system_load_pro_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    //if clicked and YES was selected from the pop-up
    if (event == MENU_EV_ENTER && item->data.hover == 0)
    {
        //current profile is the ID (A=1, B=2, C=3, D=4)
        g_current_profile = item->desc->id - item->desc->parent_id;
        item->data.value = 1;

        set_item_value(CMD_PROFILE_LOAD, g_current_profile);
    }

    else if (event == MENU_EV_NONE)
    {
        if ((item->desc->id - item->desc->parent_id) == g_current_profile)
        {
            add_chars_to_menu_name(item, option_enabled);
            item->data.value = 1;
        }
        //we dont want a [ ] behind every profile, so clear the name to just show the txt
        else
        { 
            strcpy(item->name, item->desc->name);
            item->data.value = 0;
        }
    }

    //we do not need tu update anything, a profile_update command will be run that handles that. 
}

//OVERWRITE CURRENT PROFILE
void system_save_pro_cb(void *arg, int event)
{
   menu_item_t *item = arg;

    //if clicked and YES was selected from the pop-up
    if (event == MENU_EV_ENTER && item->data.hover == 0)
    {
        set_item_value(CMD_PROFILE_STORE, g_current_profile);
        //since the current profile value cant change because of a menu enter here we do not need to update the name.
    }

    //if we are just entering the menu just add the current value to the menu item
    else if (event == MENU_EV_NONE)
    {
        char *profile_char = NULL;
        switch (g_current_profile)
        {
            case 1: profile_char = "[A]"; break;
            case 2: profile_char = "[B]"; break;
            case 3: profile_char = "[C]"; break;
            case 4: profile_char = "[D]"; break;
        }
        add_chars_to_menu_name(item, profile_char);
    }

    //we do not need to update, there is nothing that changes
}

//CV stuff
void system_cv_exp_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (g_cv_in_mode == -1)
    {
        system_input_cb(NULL, MENU_EV_ENTER);
    }

    if (event == MENU_EV_ENTER && item->data.hover == 0)
    {
        if (g_cv_in_mode == 0) g_cv_in_mode = 1;
        else g_cv_in_mode = 0;
        set_menu_item_value(MENU_ID_EXP_CV_INPUT, g_cv_in_mode);
    }
    char str_bfr[15] = {};
    strcat(str_bfr,(g_cv_in_mode ? "EXP" : "CV"));
    add_chars_to_menu_name(item, str_bfr);

    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_menu_refresh(DISPLAY_RIGHT);
}

void system_exp_mode_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (g_cv_in_mode == -1)
    {
        system_input_cb(NULL, MENU_EV_ENTER);
    }

    if (event == MENU_EV_ENTER && item->data.hover == 0)
    {
        if (g_exp_mode == 0) g_exp_mode = 1;
        else g_exp_mode = 0;
        set_menu_item_value(MENU_ID_EXP_MODE, g_exp_mode);
    }
    char str_bfr[15] = {};
    strcat(str_bfr,(g_exp_mode ? "Signal on Ring" : "Signal on Tip"));
    add_chars_to_menu_name(item, str_bfr);

    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);
}

void system_cv_hp_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (g_cv_in_mode == -1)
    {
        system_output_cb(NULL, MENU_EV_ENTER);
    }

    if (event == MENU_EV_ENTER && item->data.hover == 0)
    {
        if (g_cv_out_mode == 0) g_cv_out_mode = 1;
        else g_cv_out_mode = 0;
        set_menu_item_value(MENU_ID_HP_CV_OUTPUT, g_cv_out_mode);
    }
    char str_bfr[15] = {};
    strcat(str_bfr,(g_cv_out_mode ? "CV" : "Headphone"));
    add_chars_to_menu_name(item, str_bfr);

    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);
}

void system_pot_callibration(void *arg, int event)
{
    menu_item_t *item = arg;

    //if clicked yes, enter callibration mode
    if (event == MENU_EV_ENTER && item->data.hover == 0)
    {
        //write callibration mode screens, 0 because we start with the first potentiometer
        calibration_write_display(0);

        //toggle the mode so actuators dont toggle their normal actions
        g_calibration_mode = true; 
    }
}

void system_page_mode_cb(void *arg, int event)
{
    if (g_page_mode == -1)
    {
        //read EEPROM
        uint8_t read_buffer = 0;
        EEPROM_Read(0, PAGE_MODE_ADRESS, &read_buffer, MODE_8_BIT, 1);
        g_page_mode = read_buffer;

        //write to naveg.cs
        naveg_set_page_mode(g_page_mode);
    }

    if (event == MENU_EV_ENTER)
    {
        if (g_page_mode == 0) g_page_mode = 1;
        else g_page_mode = 0;
        
        //also write to EEPROM
        uint8_t write_buffer = g_page_mode;
        EEPROM_Write(0, PAGE_MODE_ADRESS, &write_buffer, MODE_8_BIT, 1);

        //write to naveg.c
        naveg_set_page_mode(g_page_mode);

        naveg_turn_on_pagination_leds();
    }

    if (arg != NULL)
    {
        menu_item_t *item = arg;
        add_chars_to_menu_name(item, g_page_mode ? "2 BUTTONS" : "1 BUTTON");
    }
    
    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);    
}

void system_led_brightness_cb(void *arg, int event)
{
    if (g_led_brightness == -1)
    {
        //read EEPROM
        uint8_t read_buffer = 0;
        EEPROM_Read(0, LED_BRIGHTNESS_ADRESS, &read_buffer, MODE_8_BIT, 1);
        g_led_brightness = read_buffer;

        //write to driver
        ledz_set_global_brightness(g_led_brightness);
    }

    if (event == MENU_EV_ENTER)
    {
        if (g_led_brightness < 2) g_led_brightness++;
        else g_led_brightness = 0;
        
        //also write to EEPROM
        uint8_t write_buffer = g_led_brightness;
        EEPROM_Write(0, LED_BRIGHTNESS_ADRESS, &write_buffer, MODE_8_BIT, 1);

        //write to naveg.c
        ledz_set_global_brightness(g_led_brightness);

        //update the leds 
        uint8_t j = 0;
        for (j = 0; j < LEDS_COUNT; j++)
        {
            ledz_restore_state(hardware_leds(j));
        }        
    }

    if (arg != NULL)
    {
        menu_item_t *item = arg;
        
        //get the right char to put on the screen
        char channel_value[5];
        switch (g_led_brightness)
        {
            case 0:
                    strcpy(channel_value, " Low");
                break;
            case 1:
                    strcpy(channel_value, " Mid");
                break;
            case 2:
                    strcpy(channel_value, "High");
                break;
        }
        add_chars_to_menu_name(item, channel_value);
    }
    
    //this setting changes just 1 item
    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);   
}
