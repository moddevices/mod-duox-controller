
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "device.h"

////////////////////////////////////////////////////////////////
////// SETTINGS RELATED TO HARDWARE

//// GPIO macros
#define CONFIG_PIN_INPUT(port, pin)     GPIO_SetDir((port), (1 << (pin)), GPIO_DIRECTION_INPUT)
#define CONFIG_PIN_OUTPUT(port, pin)    GPIO_SetDir((port), (1 << (pin)), GPIO_DIRECTION_OUTPUT)
#define SET_PIN(port, pin)              GPIO_SetValue((port), (1 << (pin)))
#define CLR_PIN(port, pin)              GPIO_ClearValue((port), (1 << (pin)))
#define READ_PIN(port, pin)             ((FIO_ReadValue(port) >> (pin)) & 1)
#define CONFIG_PORT_INPUT(port)         FIO_ByteSetDir((port), 0, 0xFF, GPIO_DIRECTION_INPUT)
#define CONFIG_PORT_OUTPUT(port)        FIO_ByteSetDir((port), 0, 0xFF, GPIO_DIRECTION_OUTPUT)
#define WRITE_PORT(port, value)         FIO_ByteSetValue((port), 0, (uint8_t)(value)); \
                                        FIO_ByteClearValue((port), 0, (uint8_t)(~value))
#define READ_PORT(port)                 FIO_ByteReadValue((port), (0))

//// Serial definitions
// If the UART ISR (or callbacks) uses freeRTOS API, the priorities values must be
// equal or greater than configMAX_SYSCALL_INTERRUPT_PRIORITY

// SERIAL1
#define SERIAL3
#define SERIAL3_BAUD_RATE       1500000
#define SERIAL3_PRIORITY        1
#define SERIAL3_RX_PORT         0
#define SERIAL3_RX_PIN          3
#define SERIAL3_RX_FUNC         2
#define SERIAL3_RX_BUFF_SIZE    32
#define SERIAL3_TX_PORT         0
#define SERIAL3_TX_PIN          2
#define SERIAL3_TX_FUNC         2
#define SERIAL3_TX_BUFF_SIZE    32
#define SERIAL3_HAS_OE          0

// SERIAL2
#define SERIAL2
#define SERIAL2_BAUD_RATE       115200
#define SERIAL2_PRIORITY        2
#define SERIAL2_RX_PORT         2
#define SERIAL2_RX_PIN          9
#define SERIAL2_RX_FUNC         2
#define SERIAL2_RX_BUFF_SIZE    64
#define SERIAL2_TX_PORT         2
#define SERIAL2_TX_PIN          8
#define SERIAL2_TX_FUNC         2
#define SERIAL2_TX_BUFF_SIZE    64
#define SERIAL2_HAS_OE          0

//// Hardware peripheral definitions
// Clock power control
#define HW_CLK_PWR_CONTROL      CLKPWR_PCONP_PCTIM0  | CLKPWR_PCONP_PCTIM1  |\
                                CLKPWR_PCONP_PCUART0 | CLKPWR_PCONP_PCUART3 |\
                                CLKPWR_PCONP_PCUART2 | CLKPWR_PCONP_PCSSP0  |\
                                CLKPWR_PCONP_PCGPIO  | CLKPWR_PCONP_PCADC   

//// Slots count
// One slot is a set of display, knob, footswitch and led
#define SLOTS_COUNT         2

//// LEDs configuration
// Amount of LEDS
#define LEDS_COUNT          7

// LEDs ports and pins definitions
// format definition: {R_PORT, R_PIN, G_PORT, G_PIN, B_PORT, B_PIN}
///4 actuator leds `              
#define LED0_PINS           {0, 10, 2, 15, 0, 11} //LED4
#define LED1_PINS           {2, 13, 4, 4 , 4, 17} //LED5
#define LED2_PINS           {2, 7 , 3, 16, 2, 6 } //LED6
#define LED3_PINS           {1, 12, 0, 9 , 4, 14} //LED7
 //3 "system" leds
#define LED4_PINS           {2, 24, 2, 25, 3, 26} //LED1
#define LED5_PINS           {1, 24, 4, 1 , 1, 25} //LED2
#define LED6_PINS           {2, 14, 1, 29, 4, 3 } //LED3
                               
//// GLCDs configurations
// GLCD driver, valid options: KS0108, UC1701
#define GLCD_DRIVER         UC1701
#define UC1701_REVERSE_COLUMNS
#define UC1701_REVERSE_ROWS

// Amount of displays
#define GLCD_COUNT          SLOTS_COUNT

// GCLD common definitions
// check the drivers header to see how to set the structure
#define GLCD_COMMON_CONFIG  .ssp_module = LPC_SSP0, .ssp_clock = 1000000, \
                            .ssp_clk_port = 0, .ssp_clk_pin = 15, .ssp_clk_func = 2, \
                            .ssp_mosi_port = 0, .ssp_mosi_pin = 18, .ssp_mosi_func = 2, \
                            .cd_port = 4, .cd_pin = 20

#define GLCD0_CONFIG    { GLCD_COMMON_CONFIG, \
                          .cs_port = 4, .cs_pin = 23, \
                          .rst_port = 4, .rst_pin = 8, \
                          .backlight_port = 2, .backlight_pin = 11 },

#define GLCD1_CONFIG    { GLCD_COMMON_CONFIG, \
                          .cs_port = 2, .cs_pin = 12, \
                          .rst_port = 4, .rst_pin = 5, \
                          .backlight_port = 4, .backlight_pin = 18 },

//// Actuators configuration
// Actuators IDs
enum {ENCODER0, ENCODER1, FOOTSWITCH0, FOOTSWITCH1, FOOTSWITCH2, FOOTSWITCH3, FOOTSWITCH4, FOOTSWITCH5, FOOTSWITCH6, POT0, POT1, POT2, POT3, POT4, POT5, POT6, POT7};

// Amount of footswitches
#define FOOTSWITCHES_COUNT          7
#define FOOTSWITCHES_ACTUATOR_COUNT 4

// Footswitches ports and pins definitions
// button definition: {BUTTON_PORT, BUTTON_PIN}
#define FOOTSWITCH0_PINS    {4, 16} // BTN4, actuator
#define FOOTSWITCH1_PINS    {4, 10} // BTN5, actuator
#define FOOTSWITCH2_PINS    {4, 27} // BTN6, actuator
#define FOOTSWITCH3_PINS    {0, 8 } // BTN7, actuator
#define FOOTSWITCH4_PINS    {3, 25} // BTN1, snapshot
#define FOOTSWITCH5_PINS    {2, 21} // BTN2, pages
#define FOOTSWITCH6_PINS    {4, 2 } // BTN3, snapshot

// Amount of encoders
#define ENCODERS_COUNT      SLOTS_COUNT

// Encoders ports and pins definitions
// encoder definition: {ENC_BUTTON_PORT, ENC_BUTTON_PIN, ENC_CHA_PORT, ENC_CHA_PIN, ENC_CHB_PORT, ENC_CHB_PIN}
#define ENCODER0_PINS       {4, 15, 4, 28, 1, 6}
#define ENCODER1_PINS       {1, 11, 3, 19, 0, 7}

//for encoder acceleration
//acceleration devided in x steps within 100 times the actuator clock. 
#define ENCODER_ACCEL_STEP_1            3
#define ENCODER_ACCEL_STEP_2            5
#define ENCODER_ACCEL_STEP_3            7

//amount of potentiometers
#define POTS_COUNT          8

//the resolution of a pot
#define POT_THRESHOLD       25

#define POT_LOWER_THRESHOLD (POT_THRESHOLD)
#define POT_UPPER_THRESHOLD (4095- POT_THRESHOLD)

#define POT_DIFF_THRESHOLD 300

// Potentiometers ports, pins and functions definitions
// pot definition: {POT_PORT, POT_PIN, PIN_ADC_FUNCTION, ADC CHANNEL}
#define POT0_PINS        {0, 23, 1, 0} //pot 0
#define POT1_PINS        {1, 30, 3, 4} //pot 1
#define POT2_PINS        {0, 24, 1, 1} //pot 2
#define POT3_PINS        {1, 31, 3, 5} //pot 3
#define POT4_PINS        {0, 25, 1, 2} //pot 4
#define POT5_PINS        {0, 12, 3, 6} //pot 5
#define POT6_PINS        {0, 26, 1, 3} //pot 6
#define POT7_PINS        {0, 13, 3, 7} //pot 7

#define PAGES_COUNT 3

//total amount of actuators
#define TOTAL_ACTUATORS (ENCODERS_COUNT + FOOTSWITCHES_COUNT + POTS_COUNT)

#define SHUTDOWN_BUTTON_PORT    3
#define SHUTDOWN_BUTTON_PIN     21


////////////////////////////////////////////////////////////////
////// SETTINGS RELATED TO FIRMWARE

//// webgui configuration
// define the interface
#define WEBGUI_SERIAL               3

// define how many bytes will be allocated to rx/tx buffers
#define WEBGUI_COMM_RX_BUFF_SIZE    4096
#define WEBGUI_COMM_TX_BUFF_SIZE    512

//// Protocol commands configuration
// ping0
#define PING_CMD                      "ping"
// say <message>
#define SAY_CMD                       "say %s ..."
// led <led_id> <red> <green> <blue>
#define LED_CMD                       "led %i %i %i %i ..."
// glcd_text <glcd_id> <x_pos> <y_pos> <text>
#define GLCD_TEXT_CMD                 "glcd_text %i %i %i %s"
// glcd_dialog <content>
#define GLCD_DIALOG_CMD               "glcd_dialog %s"
// glcd_draw <glcd_id> <x_pos> <y_pos> <data>
#define GLCD_DRAW_CMD                 "glcd_draw %i %i %i %s"
// ui_con
#define GUI_CONNECTED_CMD             "ui_con"
// ui_dis
#define GUI_DISCONNECTED_CMD          "ui_dis"
// control_add <hw_id> <label> <control_prop> <unit> <value> <max> <min> <steps> 
//             [scale_point_count] {[scale_point1_label] [scale_point1_value]}...
#define CONTROL_ADD_CMD               "a %i %s %i %s %f %f %f %i %i ..."
// control_rm <hw_id>
#define CONTROL_REMOVE_CMD            "rm %i ..."
// control_get <hw_id> 
#define CONTROL_GET_CMD               "g %i"
// control_set <hw_id><value>
#define CONTROL_SET_CMD               "s %i %f"
// control_next <hw_id>
#define CONTROL_NEXT_CMD              "n %i"
// control_index_set <hw_id> <current_index> <total_index>
#define CONTROL_INDEX_SET             "si %i %i %i"
// initial_state <amount of pb's> <page_min> <page_max> <bank_id> <pb_id> [current_pedalboards_list]
#define INITIAL_STATE_CMD             "is %i %i %i %i %i %s %s ..."
// banks <direction> <current banks hover id>
#define BANKS_CMD                     "banks %i %i"
// bank_config <hw_id> <function>
#define BANK_CONFIG_CMD               "bank_config %i %i"
// pedalboards <up/down page> <current page index> <bank_uid>
#define PEDALBOARDS_CMD               "pedalboards %i %i %s"
// pedalboard <bank_id> <pedalboard_uid>
#define PEDALBOARD_CMD                "pb %i %s"
// pedalboard_reset
#define PEDALBOARD_RESET_CMD          "pbr"
// pedalboard_save
#define PEDALBOARD_SAVE_CMD           "pbs"
// tuner <frequency> <note> <cents>
#define TUNER_CMD                     "tu_v %f %s %i"
// tuner on
#define TUNER_ON_CMD                  "tu on"
// tuner off
#define TUNER_OFF_CMD                 "tu off"
// tuner_input <input>
#define TUNER_INPUT_CMD               "tu_i %i"
// resp <status> ...
#define RESPONSE_CMD                  "resp %i ..."
// reboot in restore mode
#define RESTORE_CMD                   "restore"
//new duo x protocol commands
//get the name of the current pedalboard
#define PB_NAME_SET_CMD               "s_pbn %s ..."
//set display brightness  <brightness>
#define BRIGHTNESS_SET_CMD            "s_br %i"
//get the current display brightness
#define BRIGHTNESS_GET_CMD            "g_br"
// set stereo link for the inputs 
#define SL_IN_SET_CMD                 "s_il %i"                      
// get the current stereo link value for the inputs
#define SL_IN_GET_CMD                 "g_il" 
// set stereo link for the inputs 
#define SL_OUT_SET_CMD                "s_ol %i"                       
// get the current stereo link value for the inputs
#define SL_OUT_GET_CMD                "g_ol"
 // mute the audio when tuner is on
#define TUNER_MUTE_SET_CMD            "s_tum %i"         
// get the mute status of the tuner
#define TUNER_MUTE_GET_CMD            "g_tum"          
// set exp or cv input  <cv=1/exp=0>
#define EXPCV_SET_CMD                 "s_ex %i"                     
// get the current status of the cv/exp port
#define EXPCV_GET_CMD                 "g_ex"
// set headphone or cv output  <cv=1/hp=0>
#define HPCV_SET_CMD                  "s_hp %i"                     
// get the current status of the hp/cv port 
#define HPCV_GET_CMD                  "g_hp"                 
// set the quick bypass channel
#define QBP_SET_CMD                   "s_qbp %i"     
// get the quick bypass channel
#define QBP_GET_CMD                   "g_qbp"              
// toggle play status
#define PLAY_SET_CMD                  "s_ps %i"       
// get play status 
#define PLAY_GET_CMD                  "g_ps"        
// set midi clk source <internal = 0, slave = 1, link = 2>   
#define MIDI_SRC_SET_CMD              "s_cls %i"                             
// get the midi clk source channel
#define MIDI_SRC_GET_CMD              "g_cls"
//enable the midi clock <on=1/off=0>
#define SEND_MIDI_CLK_CMD             "s_mclk %i"  
//enable the midi clock <on=1/off=0>
#define GET_MIDI_CLK_ENABLE_CMD       "g_mclk"   
// set midi prog change channnel <channel>
#define MIDI_PRGCH_SET_CMD            "s_pbc %i"                          
// get midi prog change channnel 
#define MIDI_PRGCH_GET_CMD            "g_pbc"
// set midi snapchot change channnel <channel>
#define MIDI_SNAPSHOT_SET_CMD         "s_ssc %i"                                
// get midi snapchot change channnel
#define MIDI_SNAPSHOT_GET_CMD         "g_ssc"
// toggle tempo status
#define TEMPO_SET_CMD                 "s_bpm %i"                       
// get tmpo status 
#define TEMPO_GET_CMD                 "g_bpm"
// toggle beats per bar tempo status
#define BPB_SET_CMD                   "s_bpb %i"                        
// get beats per bar tempo status 
#define BPB_GET_CMD                   "g_bpb"
// toggle bypass
#define BYPASS_SET_CMD                "s_bp "                        
// get bypass status 
#define BYPASS_GET_CMD                "g_bp "
//get profile data command <int profile>
#define LOAD_PROFILE_CMD              "r_p %i"
//store the current profile in mod-ui
#define STORE_PROFILE_CMD             "s_p %i"
//get the current profile that is loaded
#define GET_PROFILE_CMD               "g_p" 
//get master volume link channel
#define MASTER_VOL_GET_LINK_CMD       "g_mv_c"
//set master volume link channel
#define MASTER_VOL_SET_LINK_CMD       "s_mv_c %i"
//request the next page <page_to_load>
#define NEXT_PAGE_COMMAND             "lp %i"
//save a snapshot <snapshot_id>
#define SAVE_SNAPSHOT_COMMAND         "ss %i"
//load a snapshot <snapshot_id>
#define LOAD_SNAPSHOT_COMMAND         "sl %i"
//clear a snapshot <snapshot_id>
#define CLEAR_SNAPSHOT_COMMAND        "ss_c"
//the HMI is ready to boot <current_page> <display_brightness>
#define BOOT_HMI_CMD                  "boot %i %i %s ..."
//get input cv bias
#define CV_BIAS_GET_CMD               "g_cvb"
//set input cv bias
#define CV_BIAS_SET_CMD               "s_cvb %i"
//get input EXP mode
#define EXP_MODE_GET_CMD              "g_exp_m"
//set input EXP mode
#define EXP_MODE_SET_CMD              "s_exp_m %i"
//change a menu setting
#define MENU_ITEM_CHANGE              "mc %i %i ..."
//clear both snapshots and controls 
#define CLEAR_PEDALBOARD              "pb_cl"
//for encoder self-test click
#define ENCODER_CLICKED_CMD           "encoder_clicked %i"
//let the HMI know if a page is available
#define PAGE_AVAILABLE_CMD            "pa %i %i %i"
//next control page <hw_id> <up/down>
#define CONTROL_PAGE_CMD              "ncp %i %i"
//save the current ADC value as calibration value
#define SAVE_POT_CAL_VAL_CMD          "save_pot_cal %i %i"


//// Control propertires definitions
#define CONTROL_PROP_LINEAR         0
#define CONTROL_PROP_BYPASS         1
#define CONTROL_PROP_TAP_TEMPO      2
#define CONTROL_PROP_ENUMERATION    4
#define CONTROL_PROP_SCALE_POINTS   8
#define CONTROL_PROP_TRIGGER        16
#define CONTROL_PROP_TOGGLED        32
#define CONTROL_PROP_LOGARITHMIC    64
#define CONTROL_PROP_INTEGER        128
#define CONTROL_PROP_REVERSE_ENUM   256

//// Banks functions definition
#define BANK_FUNC_NONE              0
#define BANK_FUNC_TRUE_BYPASS       1
#define BANK_FUNC_PEDALBOARD_NEXT   2
#define BANK_FUNC_PEDALBOARD_PREV   3
#define BANK_FUNC_AMOUNT            4

//// Tools configuration
// navigation update time, this is only useful in tool mode
#define NAVEG_UPDATE_TIME   1500

// time in milliseconds to enter in tool mode (hold rotary encoder button)
#define TOOL_MODE_TIME      500

// which display will show which tool
#define DISPLAY_TOOL_SYSTEM         0
#define DISPLAY_TOOL_TUNER          1
#define DISPLAY_TOOL_NAVIG          2
#define DISPLAY_TOOL_SYSTEM_SUBMENU 3
#define DISPLAY_TOOL_MASTER_VOL     4

//induvidual display id's
#define DISPLAY_LEFT                0
#define DISPLAY_RIGHT               1

//types of info area's 
#define INFO_PEDALBOARDNAME         0
///todo make more

//// Screen definitions
// defines the default rotary text
#define SCREEN_ROTARY_DEFAULT_NAME      "ENDLESS KNOB "
// defines the default foot text
#define SCREEN_FOOT_DEFAULT_NAME        "BUTTON "
// defines the default pot text
#define SCREEN_POT_DEFAULT_NAME         "KNOB "
// defines the default pot text in calibration
#define SCREEN_POT_CAL_DEFAULT_NAME     "KNOB #"

//// System menu configuration
// includes the system menu callbacks
#include "system.h"
// defines the menu id's
#define ROOT_ID             (0 * 10)
#define INP_ID              (1 * 10)
#define OUTP_ID             (2 * 10)
#define TUNER_ID            (3 * 10)
#define SYSTEM_ID           (4 * 10)
#define INFO_ID             (7 * 10)
#define SERVICES_ID         (8 * 10)
#define VERSIONS_ID         (9 * 10)
#define UPGRADE_ID          (10 * 10)
#define VOLUME_ID           (11 * 10)
#define DEVICE_ID           (12 * 10)
#define PEDALBOARD_ID       (13 * 10)
#define BLUETOOTH_ID        (14 * 10)
#define BANKS_ID            (15 * 10)
#define DEVICE_SETTINGS_ID  (16 * 10)
#define BYPASS_ID           (17 * 10)
#define TEMPO_ID            (18 * 10)
#define PROFILES_ID         (19 * 10)
#define MIDI_ID             (20 * 10)

//pedalboard sub menu
#define PEDALBOARD_SAVE_ID   PEDALBOARD_ID+2
#define PEDALBOARD_RESET_ID  PEDALBOARD_ID+3

//bypass sub menu
#define BP1_ID              BYPASS_ID + 1
#define BP2_ID              BYPASS_ID + 2 
#define BP12_ID             BYPASS_ID + 3
#define BP_SELECT_ID        BYPASS_ID + 4

//tempo sub menu
#define GLOBAL_TEMPO_ID    TEMPO_ID + 1
#define BEATS_PER_BAR_ID   TEMPO_ID + 2

//inputs sub menu
#define IN1_VOLUME          VOLUME_ID+0
#define IN2_VOLUME          VOLUME_ID+1
#define STEREO_LINK_INP     INP_ID+3
#define EXP_CV_INP          INP_ID+2
#define CV_RANGE            INP_ID+4
#define EXP_MODE            INP_ID+5

//output sub menu
#define OUT1_VOLUME         VOLUME_ID+2
#define OUT2_VOLUME         VOLUME_ID+3
#define HP_VOLUME           VOLUME_ID+4
#define STEREO_LINK_OUTP    OUTP_ID+3
#define HP_CV_OUTP          OUTP_ID+2
#define MASTER_VOL_LINK     OUTP_ID+4

//sync & midi sub menu
#define MIDI_CLK_SOURCE     MIDI_ID+2
#define MIDI_CLK_SEND       MIDI_ID+3
#define MIDI_SNAPSHOT       MIDI_ID+4
#define MIDI_PEDALBOARD     MIDI_ID+5

//device settings menu
#define DISP_BL_ID          DEVICE_SETTINGS_ID + 2      
#define ACTU_HIDE_ID        DEVICE_SETTINGS_ID + 3 
#define POT_LOCK_ID         DEVICE_SETTINGS_ID + 4
#define PAGE_MODE_ID        DEVICE_SETTINGS_ID + 5
#define POT_CALL_ID         DEVICE_SETTINGS_ID + 6

#define BLUETOOTH_DISCO_ID  BLUETOOTH_ID+2

#define DIALOG_ID           230

#define SYSTEM_MENU     \
    {"SETTINGS",                        MENU_LIST,      ROOT_ID,              -1,                 NULL                       , 0},  \
    {"BANKS",                           MENU_NONE,      BANKS_ID,             ROOT_ID,            system_banks_cb            , 0},  \
    {"CURRENT PEDALBOARD",              MENU_LIST,      PEDALBOARD_ID,        ROOT_ID,            NULL                       , 0},  \
    {"SAVE STATE",                      MENU_CONFIRM,   PEDALBOARD_SAVE_ID,   PEDALBOARD_ID,      system_pedalboard_cb       , 0},  \
    {"RESET STATE",                     MENU_CONFIRM,   PEDALBOARD_RESET_ID,  PEDALBOARD_ID,      system_pedalboard_cb       , 0},  \
    {"HARDWARE BYPASS",                 MENU_LIST,      BYPASS_ID,            ROOT_ID,            system_quick_bypass_cb     , 0},  \
    {"CHANNEL 1",                       MENU_TOGGLE,    BP1_ID,               BYPASS_ID,          system_bypass_cb           , 0},  \
    {"CHANNEL 2",                       MENU_TOGGLE,    BP2_ID,               BYPASS_ID,          system_bypass_cb           , 0},  \
    {"CHANNEL 1 & 2",                   MENU_TOGGLE,    BP12_ID,              BYPASS_ID,          system_bypass_cb           , 0},  \
    {"QUICK BYPASS CHANNEL(S)",         MENU_TOGGLE,    BP_SELECT_ID,         BYPASS_ID,          system_qbp_channel_cb      , 0},  \
    {"TEMPO & TRANSPORT",               MENU_LIST,      TEMPO_ID,             ROOT_ID,            system_play_cb             , 0},  \
    {"TEMPO",                           MENU_SET,       GLOBAL_TEMPO_ID,      TEMPO_ID,           system_tempo_cb            , 0},  \
    {"BEATS PER BAR",                   MENU_SET,       BEATS_PER_BAR_ID,     TEMPO_ID,           system_bpb_cb              , 0},  \
    {"TUNER",                           MENU_NONE,      TUNER_ID,             ROOT_ID,            system_tuner_cb            , 0},  \
    {"USER PROFILES",                   MENU_LIST,      PROFILES_ID,          ROOT_ID,            NULL                       , 0},  \
    {"USER PROFILE A",                  MENU_TOGGLE,    PROFILES_ID+1,        PROFILES_ID,        system_load_pro_cb         , 0},  \
    {"USER PROFILE B",                  MENU_TOGGLE,    PROFILES_ID+2,        PROFILES_ID,        system_load_pro_cb         , 0},  \
    {"USER PROFILE C",                  MENU_TOGGLE,    PROFILES_ID+3,        PROFILES_ID,        system_load_pro_cb         , 0},  \
    {"USER PROFILE D",                  MENU_TOGGLE,    PROFILES_ID+4,        PROFILES_ID,        system_load_pro_cb         , 0},  \
    {"OVERWRITE CURRENT PROFILE",       MENU_TOGGLE,    PROFILES_ID+5,        PROFILES_ID,        system_save_pro_cb         , 0},  \
    {"SYSTEM",                          MENU_LIST,      SYSTEM_ID,            ROOT_ID,            NULL                       , 0},  \
    {"INPUTS",                          MENU_LIST,      INP_ID,               SYSTEM_ID,          system_input_cb            , 0},  \
    {"< BACK TO SYSTEM",                MENU_RETURN,    INP_ID+1,             INP_ID,             NULL                       , 0},  \
    {"STEREO LINK",                     MENU_TOGGLE,    STEREO_LINK_INP,      INP_ID,             system_sl_in_cb            , 0},  \
    {"INPUT 1 GAIN",                    MENU_VOL,       IN1_VOLUME,           INP_ID,             system_volume_cb           , 0},  \
    {"INPUT 2 GAIN",                    MENU_VOL,       IN2_VOLUME,           INP_ID,             system_volume_cb           , 0},  \
    {"CV / EXP INPUT",                  MENU_TOGGLE,    EXP_CV_INP,           INP_ID,             system_cv_exp_cb           , 0},  \
    {"CV RANGE",                        MENU_TOGGLE,    CV_RANGE,             INP_ID,             system_cv_range_cb         , 0},  \
    {"EXP MODE",                        MENU_TOGGLE,    EXP_MODE,             INP_ID,             system_exp_mode_cb         , 0},  \
    {"OUTPUTS",                         MENU_LIST,      OUTP_ID,              SYSTEM_ID,          system_output_cb           , 0},  \
    {"< BACK TO SYSTEM",                MENU_RETURN,    OUTP_ID+1,            OUTP_ID,            NULL                       , 0},  \
    {"STEREO LINK ",                    MENU_TOGGLE,    STEREO_LINK_OUTP,     OUTP_ID,            system_sl_out_cb           , 0},  \
    {"OUTPUT 1 VOLUME",                 MENU_VOL,       OUT1_VOLUME,          OUTP_ID,            system_volume_cb           , 0},  \
    {"OUTPUT 2 VOLUME",                 MENU_VOL,       OUT2_VOLUME,          OUTP_ID,            system_volume_cb           , 0},  \
    {"HEADPHONE VOLUME",                MENU_VOL,       HP_VOLUME,            OUTP_ID,            system_volume_cb           , 0},  \
    {"CV / HP OUTPUT",                  MENU_TOGGLE,    HP_CV_OUTP,           OUTP_ID,            system_cv_hp_cb            , 0},  \
    {"MASTER VOLUME LINK ",             MENU_TOGGLE,    MASTER_VOL_LINK,      OUTP_ID,            system_master_vol_link_cb  , 0},  \
    {"SYNC & MIDI",                     MENU_LIST,      MIDI_ID,              SYSTEM_ID,          NULL                       , 0},  \
    {"< BACK TO SYSTEM",                MENU_RETURN,    MIDI_ID+1,            MIDI_ID,            NULL                       , 0},  \
    {"CLOCK SOURCE",                    MENU_TOGGLE,    MIDI_CLK_SOURCE,      MIDI_ID,            system_midi_src_cb         , 0},  \
    {"SEND MIDI CLOCK",                 MENU_TOGGLE,    MIDI_CLK_SEND,        MIDI_ID,            system_midi_send_cb        , 0},  \
    {"SNAPSHOT NAV MIDI CHAN",          MENU_SET,       MIDI_SNAPSHOT,        MIDI_ID,            system_ss_prog_change_cb   , 0},  \
    {"PEDALBOARD NAV MIDI CHN",         MENU_SET,       MIDI_PEDALBOARD,      MIDI_ID,            system_pb_prog_change_cb   , 0},  \
    {"BLUETOOTH",                       MENU_LIST,      BLUETOOTH_ID,         SYSTEM_ID,          system_bluetooth_cb        , 1},  \
    {"< BACK TO SYSTEM",                MENU_RETURN,    BLUETOOTH_ID+1,       BLUETOOTH_ID,       NULL                       , 0},  \
    {"ENABLE DISCOVERY",                MENU_OK,        BLUETOOTH_DISCO_ID,   BLUETOOTH_ID,       system_bluetooth_cb        , 0},  \
    {"STATUS:",                         MENU_NONE,      BLUETOOTH_ID+3,       BLUETOOTH_ID,       NULL                       , 0},  \
    {"NAME:",                           MENU_NONE,      BLUETOOTH_ID+4,       BLUETOOTH_ID,       NULL                       , 0},  \
    {"ADDRESS:",                        MENU_NONE,      BLUETOOTH_ID+5,       BLUETOOTH_ID,       NULL                       , 0},  \
    {"DEVICE SETTINGS",                 MENU_LIST,      DEVICE_SETTINGS_ID,   SYSTEM_ID,          NULL                       , 0},  \
    {"< BACK TO SYSTEM",                MENU_RETURN,    DEVICE_SETTINGS_ID+1, DEVICE_SETTINGS_ID, NULL                       , 0},  \
    {"DISPLAY BRIGHTNESS",              MENU_TOGGLE,    DISP_BL_ID,           DEVICE_SETTINGS_ID, system_display_cb          , 0},  \
    {"UNASSIGNED ACTUATORS",            MENU_TOGGLE,    ACTU_HIDE_ID,         DEVICE_SETTINGS_ID, system_hide_actuator_cb    , 0},  \
    {"KNOB MODE ",                      MENU_TOGGLE,    POT_LOCK_ID,          DEVICE_SETTINGS_ID, system_lock_pots_cb        , 0},  \
    {"PAGES MODE ",                     MENU_TOGGLE,    PAGE_MODE_ID,         DEVICE_SETTINGS_ID, system_page_mode_cb        , 0},  \
    {"POT CALIBRATION MODE",            MENU_CONFIRM,   POT_CALL_ID,          DEVICE_SETTINGS_ID, system_pot_callibration    , 0},  \
    {"INFO",                            MENU_LIST,      INFO_ID,              SYSTEM_ID,          NULL                       , 0},  \
    {"< BACK TO SYSTEM",                MENU_RETURN,    INFO_ID+1,            INFO_ID,            NULL                       , 0},  \
    {"SERVICES",                        MENU_LIST,      SERVICES_ID,          INFO_ID,            system_services_cb         , 1},  \
    {"< BACK TO INFO",                  MENU_RETURN,    SERVICES_ID+1,        SERVICES_ID,        NULL                       , 0},  \
    {"JACK:",                           MENU_NONE,      SERVICES_ID+2,        SERVICES_ID,        NULL                       , 0},  \
    {"SSHD:",                           MENU_NONE,      SERVICES_ID+3,        SERVICES_ID,        NULL                       , 0},  \
    {"MOD-UI:",                         MENU_NONE,      SERVICES_ID+4,        SERVICES_ID,        NULL                       , 0},  \
    {"DNSMASQ:",                        MENU_NONE,      SERVICES_ID+5,        SERVICES_ID,        NULL                       , 0},  \
    {"VERSIONS",                        MENU_LIST,      VERSIONS_ID,          INFO_ID,            system_versions_cb         , 0},  \
    {"< BACK TO INFO",                  MENU_RETURN,    VERSIONS_ID+1,        VERSIONS_ID,        NULL                       , 0},  \
    {"VERSION:",                        MENU_NONE,      VERSIONS_ID+2,        VERSIONS_ID,        system_release_cb          , 0},  \
    {"RESTORE:",                        MENU_NONE,      VERSIONS_ID+3,        VERSIONS_ID,        NULL                       , 0},  \
    {"SYSTEM:",                         MENU_NONE,      VERSIONS_ID+4,        VERSIONS_ID,        NULL                       , 0},  \
    {"CONTROLLER:",                     MENU_NONE,      VERSIONS_ID+5,        VERSIONS_ID,        NULL                       , 0},  \
    {"DEVICE",                          MENU_LIST,      DEVICE_ID,            INFO_ID,            NULL                       , 0},  \
    {"< BACK TO INFO",                  MENU_RETURN,    DEVICE_ID+1,          DEVICE_ID,          NULL                       , 0},  \
    {"SERIAL NUMBER",                   MENU_OK,        DEVICE_ID+2,          DEVICE_ID,          system_tag_cb              , 0},  \
    {"SYSTEM UPGRADE",                  MENU_CONFIRM,   UPGRADE_ID,           SYSTEM_ID,          system_upgrade_cb          , 0},  \
    
//POPUP DEFINES
//PROFILE POPUP TXT
#define PROFILE_POPUP_LOAD_TXT    "The device is about to load a\nnew profile. To avoid damage,\ndisconnect all devices from\nthe CV/EXP and CV/HP ports.\nContinue?"
#define PROFILE_POPUP_RELOAD_TXT  "Reload active user profile?\nThis will discard any unsaved\nchanges."
#define EXP_CV_POPUP_TXT          "The device is about to switch\ninput modes. To avoid damage,\ndisconnect all devices from\nthe CV/EXP port. Continue?"
#define HP_CV_POPUP_TXT           "The device is about to switch\noutput modes. To avoid damage,\ndisconnect all devices from\nthe CV/HP port. Continue?"
// popups text content, format : {menu_id, header_content, text_content}
#define POPUP_CONTENT   \
    {PEDALBOARD_ID, "pedalboard", "To access the selected menu\nitem, disconnect from the\ngraphical web interface."}, \
    {BANKS_ID, "Banks", "To access the selected menu\nitem, disconnect from the\ngraphical web interface."}, \
    {PEDALBOARD_SAVE_ID, "Save state", "Save current parameter values\nas the default for the active\npedalboard?"},         \
    {PEDALBOARD_RESET_ID, "Reset state", "Reset all parameter values to\nthe last saved state for the\nactive pedalboard?"},      \
    {BLUETOOTH_DISCO_ID, "Enable Bluetooth", "Bluetooth discovery mode is   now enabled for 2 minutes"},  \
    {UPGRADE_ID, "Start System Upgrade", "To start the system upgrade\nprocess, press and hold down\nthe leftmost button and press\nyes. "}, \
    {PROFILES_ID+1, "Load user profile A", PROFILE_POPUP_LOAD_TXT}, \
    {PROFILES_ID+2, "Load user profile B", PROFILE_POPUP_LOAD_TXT}, \
    {PROFILES_ID+3, "Load user profile C", PROFILE_POPUP_LOAD_TXT}, \
    {PROFILES_ID+4, "Load user profile D", PROFILE_POPUP_LOAD_TXT}, \
    {PROFILES_ID+5, "Overwrite user profile ", "Overwrite active user profile?"}, \
    {PROFILES_ID+6, "Reload user profile A", PROFILE_POPUP_RELOAD_TXT}, \
    {PROFILES_ID+7, "Reload user profile B", PROFILE_POPUP_RELOAD_TXT}, \
    {PROFILES_ID+8, "Reload user profile C", PROFILE_POPUP_RELOAD_TXT}, \
    {PROFILES_ID+9, "Reload user profile D", PROFILE_POPUP_RELOAD_TXT}, \
    {EXP_CV_INP, "Set input to EXP", EXP_CV_POPUP_TXT}, \
    {EXP_CV_INP+1, "Set input to CV",EXP_CV_POPUP_TXT}, \
    {EXP_MODE, "Change EXP mode", "To configure your expression\npedal, please check the wiring\nof your pedal"}, \
    {HP_CV_OUTP, "Set output to HP", HP_CV_POPUP_TXT}, \
    {HP_CV_OUTP+1, "Set output to CV", HP_CV_POPUP_TXT}, \
    {POT_CALL_ID, "knob calibration mode", "enter knob calibration mode?\nthis mode is used to\ncalibrate the range of the\nanalog potentiometers.\nThis mode locks the menu\nfrom other actions temporarily"}, \

#define MENU_LINE_CHARS     31

//ID's for menu callback funtions 
//THIS SHOULD NOT BE HERE ALMOST ALL OF THEM ARE ALREDY DEFINE ABOVE
// TODO REMOVE THIS AND ADD THE CORRECT ONES TO SYSTEM.C
#define PLAY_STATUS_ID        180
#define TUNER_MUTE_ID         30
#define BYPASS1_ID            171
#define BYPASS2_ID            172
#define QUICK_BYPASS_ID       174
#define STEREOLINK_INP_ID     13
#define STEREOLINK_OUTP_ID    23
#define MASTER_VOL_PORT_ID    24
#define MIDI_CLK_SOURCE_ID    202
#define MIDI_CLK_SEND_ID      203
#define SNAPSHOT_PRGCHGE_ID   204
#define PB_PRGCHNGE_ID        205
#define DISPLAY_BRIGHTNESS_ID 161

#define AMOUNT_OF_MENU_VARS   13

//// Button functions leds colors
#define TOGGLED_COLOR           RED
#define TRIGGER_COLOR           WHITE
#define TRIGGER_PRESSED_COLOR   RED
#define TAP_TEMPO_COLOR         WHITE
#define ENUMERATED_COLOR        WHITE
#define BYPASS_COLOR            RED
#define PAGES1_COLOR            RED
#define PAGES2_COLOR            YELLOW
#define PAGES3_COLOR            CYAN
#define SNAPSHOT_COLOR          WHITE
#define SNAPSHOT_LOAD_COLOR     CYAN

//// Tap Tempo
// defines the time that the led will stay turned on (in milliseconds)
#define TAP_TEMPO_TIME_ON       100
// defines the default timeout value (in milliseconds)
#define TAP_TEMPO_DEFAULT_TIMEOUT 3000
// defines the difference in time the taps can have to be registered to the same sequence (in milliseconds)
#define TAP_TEMPO_TAP_HYSTERESIS 100
// defines the time (in milliseconds) that the tap can be over the maximum value to be registered
#define TAP_TEMPO_MAXVAL_OVERFLOW 50

//time in us for the display brightness + led tick interupt
#define LED_DISPL_INTERUPT_TIME     100

//// Toggled
// defines the toggled footer text
#define TOGGLED_ON_FOOTER_TEXT      "ON"
#define TOGGLED_OFF_FOOTER_TEXT     "OFF"

//// Bypass
// defines the bypass footer text
#define BYPASS_ON_FOOTER_TEXT       "OFF"
#define BYPASS_OFF_FOOTER_TEXT      "ON"

//// Bank configuration functions
// defines the true bypass footer text
#define TRUE_BYPASS_FOOTER_TEXT     "TRUE BYPASS"
// defines the next pedalboard footer text
#define PEDALBOARD_NEXT_FOOTER_TEXT "+"
// defines the previous pedalboard footer text
#define PEDALBOARD_PREV_FOOTER_TEXT "-"

//// Command line interface configurations
// defines the cli serial
#define CLI_SERIAL                  2
// defines how much time wait for console response (in milliseconds)
#define CLI_RESPONSE_TIMEOUT        500

///EEPROM adress page defines
#define EEPROM_EMPTY_CHECK_ADRESS          62
#define HIDE_ACTUATOR_ADRESS               0
#define LOCK_POTENTIOMTERS_ADRESS          1
#define DISPLAY_BRIGHTNESS_ADRESS          2
#define SYS_BUTTON_CONFIG_ADRESS           3
#define POT_1_MIN_CALIBRATION_ADRESS       4
#define POT_1_MAX_CALIBRATION_ADRESS       6
#define POT_2_MIN_CALIBRATION_ADRESS       8
#define POT_2_MAX_CALIBRATION_ADRESS       10
#define POT_3_MIN_CALIBRATION_ADRESS       12
#define POT_3_MAX_CALIBRATION_ADRESS       14   
#define POT_4_MIN_CALIBRATION_ADRESS       16
#define POT_4_MAX_CALIBRATION_ADRESS       18
#define POT_5_MIN_CALIBRATION_ADRESS       20
#define POT_5_MAX_CALIBRATION_ADRESS       22
#define POT_6_MIN_CALIBRATION_ADRESS       24    
#define POT_6_MAX_CALIBRATION_ADRESS       26
#define POT_7_MIN_CALIBRATION_ADRESS       28
#define POT_7_MAX_CALIBRATION_ADRESS       30
#define POT_8_MIN_CALIBRATION_ADRESS       32
#define POT_8_MAX_CALIBRATION_ADRESS       34
#define PAGE_MODE_ADRESS                   35

// these macros should be used in replacement to default malloc and free functions of stdlib.h
// The FREE function is NULL safe
#include "FreeRTOS.h"
#define MALLOC(n)       pvPortMalloc(n)
#define FREE(pv)        vPortFree(pv)


////////////////////////////////////////////////////////////////
////// DON'T CHANGE THIS DEFINES

//// Actuators types
#define NONE            0
#define FOOT            1
#define KNOB            2
#define POTENTIOMETER   3

// serial count definition
#define SERIAL_COUNT    4

// check serial rx buffer size
#ifndef SERIAL0_RX_BUFF_SIZE
#define SERIAL0_RX_BUFF_SIZE    0
#endif
#ifndef SERIAL1_RX_BUFF_SIZE
#define SERIAL1_RX_BUFF_SIZE    0
#endif
#ifndef SERIAL2_RX_BUFF_SIZE
#define SERIAL2_RX_BUFF_SIZE    0
#endif
#ifndef SERIAL3_RX_BUFF_SIZE
#define SERIAL3_RX_BUFF_SIZE    0
#endif

// check serial tx buffer size
#ifndef SERIAL0_TX_BUFF_SIZE
#define SERIAL0_TX_BUFF_SIZE    0
#endif
#ifndef SERIAL1_TX_BUFF_SIZE
#define SERIAL1_TX_BUFF_SIZE    0
#endif
#ifndef SERIAL2_TX_BUFF_SIZE
#define SERIAL2_TX_BUFF_SIZE    0
#endif
#ifndef SERIAL3_TX_BUFF_SIZE
#define SERIAL3_TX_BUFF_SIZE    0
#endif

#define SERIAL_MAX_RX_BUFF_SIZE     MAX(MAX(SERIAL0_RX_BUFF_SIZE, SERIAL1_RX_BUFF_SIZE), \
                                        MAX(SERIAL2_RX_BUFF_SIZE, SERIAL3_RX_BUFF_SIZE))

#define SERIAL_MAX_TX_BUFF_SIZE     MAX(MAX(SERIAL0_TX_BUFF_SIZE, SERIAL1_TX_BUFF_SIZE), \
                                        MAX(SERIAL2_TX_BUFF_SIZE, SERIAL3_TX_BUFF_SIZE))

// GLCD configurations definitions
#ifndef GLCD0_CONFIG
#define GLCD0_CONFIG
#endif

#ifndef GLCD1_CONFIG
#define GLCD1_CONFIG
#endif

#ifndef GLCD2_CONFIG
#define GLCD2_CONFIG
#endif

#ifndef GLCD3_CONFIG
#define GLCD3_CONFIG
#endif

// GLCD drivers definitions
#define KS0108      0
#define UC1701      1

// GLCD driver include
#if GLCD_DRIVER == KS0108
#include "ks0108.h"
#elif GLCD_DRIVER == UC1701
#include "uc1701.h"
#endif

#endif
