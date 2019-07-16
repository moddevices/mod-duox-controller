
/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include "system.h"
#include "config.h"
#include "data.h"
#include "naveg.h"
#include "hardware.h"
#include "actuator.h"
#include "comm.h"
#include "cli.h"
#include "screen.h"
#include "glcd_widget.h"
#include "glcd.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>


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
static uint8_t g_comm_protocol_bussy = 0;
float g_gains_volumes[5] = {};
uint8_t g_master_vol_port = 0;
uint8_t g_q_bypass = 0;
uint8_t g_bypass[4] = {};
uint8_t g_current_profile = 1;
uint8_t g_quick_bypass_channel = 0;
uint8_t g_sl_out = 0;
uint8_t g_sl_in = 0;
uint8_t g_snapshot_prog_change = 0;
uint8_t g_pedalboard_prog_change = 0;
uint8_t g_beats_per_minute = 0;
uint8_t g_beats_per_bar = 0;
uint8_t g_MIDI_clk_send = 0;
uint8_t g_MIDI_clk_src = 0;
uint8_t g_play_status = 0;
uint8_t g_tuner_mute = 0;
uint8_t g_display_brightness = 2;

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
void set_item_value(char *command, uint8_t value)
{
    if (g_comm_protocol_bussy) return;

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
    comm_webgui_set_response_cb(NULL, NULL);

    // sends the data to GUI
    comm_webgui_send(buffer, i);
}

static void volume(menu_item_t *item, int event, const char *source, float min, float max, float step)
{
    char value[8] = {};
    static const char *response = NULL;
    cli_command(NULL, CLI_DISCARD_RESPONSE);
    uint8_t dir = (source[0] == 'i') ? 0 : 1;

    if (((event == MENU_EV_UP) || (event == MENU_EV_DOWN)) && (dir ? g_sl_out : g_sl_in) && (item->desc->id != HP_VOLUME))
    {
        //change volume for both
        //PGA (input)
        if (!dir)
        {
            float_to_str(item->data.value, value, 8, 1);
            cli_command("mod-amixer in 0 xvol ", CLI_CACHE_ONLY);
            cli_command(value, CLI_DISCARD_RESPONSE);
        }
        //DAC (output)
        else
        {
            float_to_str(item->data.value, value, 8, 1);
            cli_command("mod-amixer out 0 xvol ", CLI_CACHE_ONLY);
            cli_command(value, CLI_DISCARD_RESPONSE);
        }
    }
    else
    {
        if ((event == MENU_EV_ENTER) || (event == MENU_EV_NONE))
        {
            cli_command("mod-amixer ", CLI_CACHE_ONLY);
            cli_command(source, CLI_CACHE_ONLY);
            cli_command(" xvol", CLI_CACHE_ONLY);
            response = cli_command(NULL, CLI_RETRIEVE_RESPONSE);

            char str[LINE_BUFFER_SIZE+1];
            strcpy(str, response);

            item->data.min = min;
            item->data.max = max;
            item->data.step = step;

            int res = 0;  // Initialize result
            int sign = 1;  // Initialize sign as positive
            int i = 0;  // Initialize index of first digit

            // If number is negative, then update sign
            if (str[0] == '-')
            {
                sign = -1;
                i++;  // Also update index of first digit
            }

            // Iterate through all digits and update the result
            for (; str[i] != '.'; ++i)
                res = res*10 + (int)str[i] - 48;

            // Return result with sign
            item->data.value = sign*res;

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

    char str_bfr[8] = {};
    float value_bfr = MAP(item->data.value, min, max, 0, 100);
    int_to_str(value_bfr, str_bfr, 8, 0);
    strcpy(item->name, item->desc->name);
    uint8_t q;
    uint8_t value_size = strlen(str_bfr);
    uint8_t name_size = strlen(item->name);
    for (q = 0; q < (31 - name_size - value_size - 1); q++)
    {
        strcat(item->name, " ");
    }
    strcat(item->name, str_bfr);
    strcat(item->name, "%");

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

void system_lock_comm_serial(uint8_t bussy)
{
    g_comm_protocol_bussy = bussy;
}

uint8_t system_get_current_profile(void)
{
    return g_current_profile;
}

//I'm not a system callback
float system_master_volume_cb(float value, int event)
{
    //what is the master volume currently connected to? and convert it to a char
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
        float_to_str(value, value_char, 8, 1);
        cli_command("mod-amixer out ", CLI_CACHE_ONLY);
        cli_command(channel_char, CLI_CACHE_ONLY);
        cli_command(" xvol ", CLI_CACHE_ONLY);
        cli_command(value_char, CLI_DISCARD_RESPONSE);
        
        return value;
    }
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

void system_update_menu_value(uint8_t item_ID, uint8_t value)
{
    switch(item_ID)
    {
        //play status
        case PLAY_STATUS_ID:
            g_play_status = value;
        break;
        //global tempo
        case GLOBAL_TEMPO_ID:
            g_beats_per_minute = value;
        break;
        //global tempo status
        case BEATS_PER_BAR_ID:
            g_beats_per_bar = value;
        break;
        //tuner mute
        case TUNER_MUTE_ID: 
            g_tuner_mute = value;
        break;
        //bypass channel 1
        case BYPASS1_ID: 
            g_bypass[0] = value;
        break;
        //bypass channel 2
        case BYPASS2_ID: 
            g_bypass[1] = value;
        break;
        //quick bypass channel
        case QUICK_BYPASS_ID: 
            g_q_bypass = value;
        break;
        //sl input
        case STEREOLINK_INP_ID: 
            g_sl_in = value;
        break;
        //master volume target 
        case MASTER_VOL_PORT_ID: 
            g_master_vol_port = value;
        break;
        //stereo link output
        case STEREOLINK_OUTP_ID: 
            g_sl_out = value;
        break;
        //MIDI clock source
        case MIDI_CLK_SOURCE_ID: 
            g_MIDI_clk_src = value;
        break;
        //send midi clock
        case MIDI_CLK_SEND_ID: 
            g_MIDI_clk_send = value;
        break;
        //snapshot prog change 
        case SNAPSHOT_PRGCHGE_ID: 
            g_snapshot_prog_change = value;
        break;
        //pedalboard prog change 
        case PB_PRGCHNGE_ID: 
            g_pedalboard_prog_change = value;
        break;
        //user profile change 
        case PROFILES_ID: 
            g_current_profile = value;
        break;
        //display brightness
        case DISPLAY_BRIGHTNESS_ID: 
            g_display_brightness = value;
            hardware_glcd_brightness(g_display_brightness); 
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
                comm_webgui_send(PEDALBOARD_SAVE_CMD, strlen(PEDALBOARD_SAVE_CMD));
                break;

            case PEDALBOARD_RESET_ID:
                comm_webgui_send(PEDALBOARD_RESET_CMD, strlen(PEDALBOARD_RESET_CMD));
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
        if (item->desc->id == BLUETOOTH_ID)
        {
            response = cli_command("mod-bluetooth status", CLI_RETRIEVE_RESPONSE);
            update_status(item->data.list[2], response);
            response = cli_command("mod-bluetooth name", CLI_RETRIEVE_RESPONSE);
            update_status(item->data.list[3], response);
            response = cli_command("mod-bluetooth address", CLI_RETRIEVE_RESPONSE);
            update_status(item->data.list[4], response);
        }
        else if (item->desc->id == BLUETOOTH_DISCO_ID)
        {
            cli_command("mod-bluetooth discovery", CLI_DISCARD_RESPONSE);
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
                max = 78.0;
                step = 1.0;
                break;

            case IN2_VOLUME:
                source = "in 2";
                min = 0.0;
                max = 78.0;
                step = 1.0;
                break;

            case OUT1_VOLUME:
                source = "out 1";
                min = -60.0;
                max = 0.0;
                step = 2.0;
                break;

            case OUT2_VOLUME:
                source = "out 2";
                min = -60.0;
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

void system_master_vol_link_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_master_vol_port < 2)
        {
            g_master_vol_port++;

            //if stereo link is enabled, disable it
            if (g_sl_out == 1)
            {
                g_sl_out = 0;
                set_item_value(SL_OUT_SET_CMD, g_sl_out);
            }
        }
        else
        {
            g_master_vol_port = 0;
            //if value is 0 (link to 1&2) we must also turn on stereo link fo the output
            g_sl_out = 1;
            set_item_value(SL_OUT_SET_CMD, g_sl_out);

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
        set_item_value(MASTER_VOL_SET_LINK_CMD, g_master_vol_port);
    }

    char str_bfr[4];
    switch (g_master_vol_port)
    {
        case 0:
            strcpy(str_bfr,"1&2");
        break;
        case 1:
            strcpy(str_bfr,"1");
        break;
        case 2:
            strcpy(str_bfr,"2");
        break;
    }
    add_chars_to_menu_name(item, str_bfr);

    //the whole menu can be changed, lets update everything
    if (event == MENU_EV_ENTER) naveg_menu_refresh(DISPLAY_RIGHT);
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
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_display_brightness < MAX_BRIGHTNESS) g_display_brightness++;
        else g_display_brightness = 0;

        hardware_glcd_brightness(g_display_brightness); 
        set_item_value(BRIGHTNESS_SET_CMD, g_display_brightness);
    }

    char str_bfr[8];
    int_to_str((g_display_brightness * 25), str_bfr, 4, 0);
    strcat(str_bfr, "%");
    add_chars_to_menu_name(item, str_bfr);
}

void system_sl_in_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (g_sl_in == 0) g_sl_in = 1;
        else g_sl_in = 0;

        set_item_value(SL_IN_SET_CMD, g_sl_in);

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
            
            //we also need to change the master volume link to 0 (1&2)
            g_master_vol_port = 0;

            naveg_update_gain(DISPLAY_RIGHT, OUT2_VOLUME, g_gains_volumes[OUT1_VOLUME - VOLUME_ID], 0, 78);

            system_save_gains_cb(NULL, MENU_EV_ENTER);
        }
        else 
        {
            g_sl_out = 0;

            //we must change the master volume link as wel to channel 1 =1 
            g_master_vol_port = 1;
        }
        set_item_value(MASTER_VOL_SET_LINK_CMD, g_master_vol_port);
        set_item_value(SL_OUT_SET_CMD, g_sl_out);
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
        set_item_value(TUNER_MUTE_SET_CMD, g_tuner_mute);
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
        set_item_value(PLAY_SET_CMD, g_play_status);
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
        set_item_value(MIDI_SRC_SET_CMD, g_MIDI_clk_src);
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
        set_item_value(SEND_MIDI_CLK_CMD, g_MIDI_clk_send);
    }

    add_chars_to_menu_name(item, (g_MIDI_clk_send? option_enabled : option_disabled));
}

void system_ss_prog_change_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        set_item_value(MIDI_SNAPSHOT_SET_CMD, item->data.value);
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
        set_item_value(MIDI_SNAPSHOT_SET_CMD, g_snapshot_prog_change);
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
        set_item_value(MIDI_PRGCH_SET_CMD, item->data.value);
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
        set_item_value(MIDI_PRGCH_SET_CMD, g_pedalboard_prog_change);
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
        if (g_MIDI_clk_src != 1) set_item_value(TEMPO_SET_CMD, item->data.value);
    }
    else if (event == MENU_EV_NONE)
    {
        //set the item value to the bpm since mod-ui is master
        item->data.value =  g_beats_per_minute;
        item->data.min = 20;
        item->data.max = 220;
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
            set_item_value(TEMPO_SET_CMD, g_beats_per_minute);
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
        set_item_value(BPB_SET_CMD, item->data.value);
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
        set_item_value(BPB_SET_CMD, g_beats_per_bar);
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
    char cmd_bfr[32];
    char channel[8];

    //0=in1, 1=in2, 2=in1&2
    switch (item->desc->id)
    {
        //in 1
        case BP1_ID:
            //we need to toggle the bypass
            if (event == MENU_EV_ENTER)
            {
                //add channel to the command 
                strcpy(cmd_bfr, BYPASS_SET_CMD);
                int_to_str(0, channel, 4, 0);
                strcat(cmd_bfr, channel);
                strcat(cmd_bfr, " ");
                //we toggle the bypass 
                g_bypass[0] = !g_bypass[0];
                set_item_value(cmd_bfr, g_bypass[0]);
            }
            add_chars_to_menu_name(item, (g_bypass[0]? option_enabled : option_disabled));
        break;

        //in2
        case BP2_ID:
            //we need to toggle the bypass
            if (event == MENU_EV_ENTER)
            {
                //add channel to the command 
                strcpy(cmd_bfr, BYPASS_SET_CMD);
                int_to_str(1, channel, 4, 0);
                strcat(cmd_bfr, channel);
                strcat(cmd_bfr, " ");
                //we toggle the bypass 
                g_bypass[1] = !g_bypass[1];
                set_item_value(cmd_bfr, g_bypass[1]);
            }
            add_chars_to_menu_name(item, (g_bypass[1]? option_enabled : option_disabled));
        break;

        case BP12_ID:
            if (event == MENU_EV_ENTER)
            {
                //add channel to the command 
                strcpy(cmd_bfr, BYPASS_SET_CMD);
                int_to_str(2, channel, 4, 0);
                strcat(cmd_bfr, channel);
                strcat(cmd_bfr, " ");
                //toggle the bypasses
                g_bypass[2] = !g_bypass[2];
                set_item_value(cmd_bfr, g_bypass[2]);
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
        set_item_value(QBP_SET_CMD, g_q_bypass);
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
    char cmd_bfr[32];
    char channel[8];

    menu_item_t *item = arg;

    char str_bfr[15] = {};

    //bypass[0] = in1, bypass[1] = in2
    switch(g_q_bypass)
    {
        //bypass 1
        case (0):
            if (event == MENU_EV_ENTER)
            {
                //add channel to the command 
                strcpy(cmd_bfr, BYPASS_SET_CMD);
                int_to_str(0, channel, 4, 0);
                strcat(cmd_bfr, channel);
                strcat(cmd_bfr, " ");
                //we toggle the bypass 
                g_bypass[0] = !g_bypass[0];
                set_item_value(cmd_bfr, g_bypass[0]);
            }
            strcpy(str_bfr,"BYPASS ");
            strcat(str_bfr, (g_bypass[0]? option_enabled : option_disabled));
            add_chars_to_menu_name(item, str_bfr);
        break;
        //bypass 2
        case (1):
            if (event == MENU_EV_ENTER)
            {
                //add channel to the command 
                strcpy(cmd_bfr, BYPASS_SET_CMD);
                int_to_str(1, channel, 4, 0);
                strcat(cmd_bfr, channel);
                strcat(cmd_bfr, " ");
                //we toggle the bypass 
                g_bypass[1] = !g_bypass[1];
                set_item_value(cmd_bfr, g_bypass[1]);
            }
            strcpy(str_bfr,"BYPASS ");
            strcat(str_bfr, (g_bypass[1]? option_enabled : option_disabled));
            add_chars_to_menu_name(item, str_bfr);
        break;
        //bypass 1&2
        case (2):
            if (event == MENU_EV_ENTER)
            {
                //add channel to the command 
                strcpy(cmd_bfr, BYPASS_SET_CMD);
                int_to_str(2, channel, 4, 0);
                strcat(cmd_bfr, channel);
                strcat(cmd_bfr, " ");
                //we toggle the bypass
                g_bypass[2] = !g_bypass[2];
                set_item_value(cmd_bfr, g_bypass[2]);
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
        item->data.value = g_current_profile;

        set_item_value(LOAD_PROFILE_CMD, g_current_profile);
    }

    else if (event == MENU_EV_NONE)
    {
        if ((item->desc->id - item->desc->parent_id) == g_current_profile)
        {
            add_chars_to_menu_name(item, option_enabled);
        }
        //we dont want a [ ] behind every profile, so clear the name to just show the txt
        else strcpy(item->name, item->desc->name);
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
        set_item_value(STORE_PROFILE_CMD, g_current_profile);
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

//ONLY NEEDED ONCE CV IS WORKING, NOT USED RIGHT NOW
/*
void system_cv_exp_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER || event == MENU_EV_NONE)
    {
        if (event == MENU_EV_ENTER && item->data.hover == 0)
        {
            if (item->data.value == 0) item->data.value = 1;
            else item->data.value = 0;
            set_item_value(EXPCV_SET_CMD, item->data.value);
        }
        else if (event == MENU_EV_NONE)
        {
            item->data.value = 0;
            request_item_value(EXPCV_GET_CMD, item);
        }

        strcpy(item->name, item->desc->name);
        uint8_t q;
        uint8_t value_size = 3;
        uint8_t name_size = strlen(item->name);

        for (q = 0; q < (31 - name_size - value_size); q++)
        {
            strcat(item->name, " ");
        }

        strcat(item->name, (item->data.value ? "EXP" : " CV"));

        if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);
    }
}

void system_cv_hp_cb (void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER || event == MENU_EV_NONE)
    {
        if (event == MENU_EV_ENTER && item->data.hover == 0)
        {
            if (item->data.value == 0) item->data.value = 1;
            else item->data.value = 0;
            set_item_value(HPCV_SET_CMD, item->data.value);
        }
        else if (event == MENU_EV_NONE)
        {
            item->data.value = 0;
            request_item_value(HPCV_GET_CMD, item);
        }

        strcpy(item->name, item->desc->name);
        uint8_t q;
        uint8_t value_size = 2;
        uint8_t name_size = strlen(item->name);
        for (q = 0; q < (31 - name_size - value_size); q++)
        {
            strcat(item->name, " ");
        }
        strcat(item->name, (item->data.value ? "HP" : "CV"));
        if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);
    }
}

void system_cv_range_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (item->data.value < 1) item->data.value++;
        else item->data.value = 0;
        set_item_value(CV_BIAS_SET_CMD, item->data.value);
    }
    else if (event == MENU_EV_NONE)
    {
        request_item_value(CV_BIAS_GET_CMD, item);
        item->data.value = 0;
    }

    char str_bfr[16];
    if (item->data.value == 0) strcpy(str_bfr,"0 to +5V");
    else if (item->data.value == 1) strcpy(str_bfr,"-2.5 to +2.5V");

    strcpy(item->name, item->desc->name);
    uint8_t q;
    uint8_t value_size = strlen(str_bfr);
    uint8_t name_size = strlen(item->name);
    for (q = 0; q < (31 - name_size - value_size); q++)
    {
        strcat(item->name, " ");
    }
    strcat(item->name, (str_bfr));

    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);
}

void system_exp_mode_cb(void *arg, int event)
{
    menu_item_t *item = arg;

    if (event == MENU_EV_ENTER)
    {
        if (item->data.value < 1) item->data.value++;
        else item->data.value = 0;
        set_item_value(EXP_MODE_SET_CMD, item->data.value);
    }
    else if (event == MENU_EV_NONE)
    {
        item->data.value = 0;
        request_item_value(EXP_MODE_GET_CMD, item);
    }

    char str_bfr[16];
    if (item->data.value == 0) strcpy(str_bfr,"Signal on tip");
    else if (item->data.value == 1) strcpy(str_bfr,"Signal on ring");

    strcpy(item->name, item->desc->name);
    uint8_t q;
    uint8_t value_size = strlen(str_bfr);
    uint8_t name_size = strlen(item->name);
    for (q = 0; q < (31 - name_size - value_size); q++)
    {
        strcat(item->name, " ");
    }
    strcat(item->name, (str_bfr));

    if (event == MENU_EV_ENTER) naveg_settings_refresh(DISPLAY_RIGHT);
}
*/