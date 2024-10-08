/*
************************************************************************************************************************
*           INCLUDE FILES
************************************************************************************************************************
*/

#include "naveg.h"
#include "node.h"
#include "config.h"
#include "screen.h"
#include "utils.h"
#include "ledz.h"
#include "hardware.h"
#include "ui_comm.h"
#include "sys_comm.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "actuator.h"
#include "calibration.h"
#include "mod-protocol.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <float.h>

//reset actuator queue
void reset_queue(void);

/*
************************************************************************************************************************
*           LOCAL DEFINES
************************************************************************************************************************
*/

enum {TT_INIT, TT_COUNTING};
enum {TOOL_OFF, TOOL_ON};
enum {BANKS_LIST, PEDALBOARD_LIST};

#define MAX_CHARS_MENU_NAME     (128/4)
#define MAX_TOOLS               5

#define DIALOG_MAX_SEM_COUNT   1

#define PAGE_DIR_DOWN       0
#define PAGE_DIR_UP         1
#define PAGE_DIR_INIT       2

/*
************************************************************************************************************************
*           LOCAL CONSTANTS
************************************************************************************************************************
*/

static menu_desc_t g_menu_desc[] = {
    SYSTEM_MENU
    {NULL, 0, -1, -1, NULL, 0}
};

static const menu_popup_t g_menu_popups[] = {
    POPUP_CONTENT
    {-1, NULL, NULL}
};


/*
************************************************************************************************************************
*           LOCAL DATA TYPES
************************************************************************************************************************
*/

struct TAP_TEMPO_T {
    uint32_t time, max;
    uint8_t state;
} g_tap_tempo[FOOTSWITCHES_ACTUATOR_COUNT];

struct TOOL_T {
    uint8_t state, display;
} g_tool[MAX_TOOLS];


/*
************************************************************************************************************************
*           LOCAL MACROS
************************************************************************************************************************
*/
#define MAP(x, Imin, Imax, Omin, Omax)      ( x - Imin ) * (Omax -  Omin)  / (Imax - Imin) + Omin;
/*
************************************************************************************************************************
*           LOCAL GLOBAL VARIABLES
************************************************************************************************************************
*/

static control_t *g_encoders[ENCODERS_COUNT], *g_foots[FOOTSWITCHES_ACTUATOR_COUNT], *g_pots[POTS_COUNT];
static bp_list_t *g_banks, *g_naveg_pedalboards;
static uint16_t g_bp_state, g_current_pedalboard, g_bp_first;
static node_t *g_menu, *g_current_menu, *g_current_main_menu;
static menu_item_t *g_current_item, *g_current_main_item;
static uint8_t g_max_items_list;
static bank_config_t g_bank_functions[BANK_FUNC_COUNT];
static uint8_t g_initialized, g_ui_connected;
static void (*g_update_cb)(void *data, int event);
static void *g_update_data;
static xSemaphoreHandle g_dialog_sem;
static uint8_t snapshot_loaded[3] = {};
static uint8_t page = 0;
static int16_t g_current_bank;
static uint8_t page_available[6] = {1, 1, 1, 1, 1, 1};
static uint8_t g_lock_potentiometers = 1;
//default scrolling direction, will only change and set back once needed
static uint8_t g_scroll_dir = 1;
static uint8_t g_page_mode = 0;
static uint8_t g_reboot_value = 0;

// only disabled after "boot" command received
bool g_self_test_mode = true;
bool g_self_test_cancel_button = false;
float g_pot_calibrations[2][POTS_COUNT] = {{0}};
bool g_device_booted = false;
bool g_dialog_active = false;
bool g_pedalboards_need_update = false;

/*
************************************************************************************************************************
*           LOCAL FUNCTION PROTOTYPES
************************************************************************************************************************
*/

static void display_encoder_add(control_t *control);
static void display_encoder_rm(uint8_t hw_id);

static void display_pot_add(control_t *control);
static void display_pot_rm(uint8_t hw_id);

static void foot_control_add(control_t *control);
static void foot_control_rm(uint8_t hw_id);

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

static void tool_on(uint8_t tool, uint8_t display)
{
    g_tool[tool].state = TOOL_ON;
    g_tool[tool].display = display;
}

static void tool_off(uint8_t tool)
{
    g_tool[tool].state = TOOL_OFF;
}

static int tool_is_on(uint8_t tool)
{
    return g_tool[tool].state;
}

static int get_display_by_id(uint8_t id, uint8_t type)
{
    if (type == POTENTIOMETER)
    {
      if (id >= 4)
          return 1;
      else
          return 0;
    }

    else if (type == FOOT)
    {
        if (id >= 2)
            return 1;
        else
            return 0;
    }
    //error
    return 0;
}

static void display_disable_all_tools(uint8_t display)
{
    int i;

    if (tool_is_on(DISPLAY_TOOL_TUNER))
    {
        ui_comm_webgui_send(CMD_TUNER_OFF, strlen(CMD_TUNER_OFF));
    }

    for (i = 0; i < MAX_TOOLS; i++)
    {
        if (g_tool[i].display == display)
            g_tool[i].state = TOOL_OFF;
    }
}

static int display_has_tool_enabled(uint8_t display)
{
    int i;
    for (i = 0; i < MAX_TOOLS; i++)
    {
        if (g_tool[i].display == display && g_tool[i].state == TOOL_ON)
            return 1;
    }

    return 0;
}

void draw_all_pots(uint8_t display)
{
    uint8_t i = 0;
    uint8_t pot = 0;
    if (display == 1)
        pot = 4;

    for (i = 0; i < 4; i++)
    {
        screen_pot((pot + i), g_pots[pot + i]);
    }
}

void draw_all_foots(uint8_t display)
{
    uint8_t i;
    uint8_t foot = 0;
    if (display == 1)
        foot = 2;

    for (i = 0; i < 2; i++)
    {
        // checks the function assigned to foot and update the footer
        if (g_foots[foot + i])
        {
            //prevent toggling of pressed light
            g_foots[foot + i]->scroll_dir = 2;

            foot_control_add(g_foots[foot + i]);
        }
        else
        {
            screen_footer(foot + i, NULL, NULL, 0);
        }
    }
}

// search the control
static control_t *search_encoder(uint8_t hw_id)
{
    uint8_t i;
    control_t *control;

    for (i = 0; i < SLOTS_COUNT; i++)
    {
        control = g_encoders[i];
        if (control)
        {
            if (hw_id == control->hw_id)
            {
                return control;
            }
        }
    }
    return NULL;
}

// calculates the control value using the step
static void step_to_value(control_t *control)
{
    // about the calculation: http://lv2plug.in/ns/ext/port-props/#rangeSteps

    float p_step = ((float) control->step) / ((float) (control->steps - 1));
    if (control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS))
    {
        control->value = control->scale_points[control->step]->value;
    }
    else if (control->properties & FLAG_CONTROL_LOGARITHMIC)
    {
        control->value = control->minimum * pow(control->maximum / control->minimum, p_step);
    }
    else if (!(control->properties & (FLAG_CONTROL_TRIGGER | FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS)))
    {
        control->value = (p_step * (control->maximum - control->minimum)) + control->minimum;
    }

    if (control->value > control->maximum)
        control->value = control->maximum;
    if (control->value < control->minimum)
        control->value = control->minimum;
}

// control assigned to display
static void display_encoder_add(control_t *control)
{
    if (control->hw_id >= ENCODERS_COUNT) return;

    uint8_t display = control->hw_id;

    // checks if is already a control assigned in this display and remove it
    if (g_encoders[display])
        data_free_control(g_encoders[display]);

    // assign the new control
    g_encoders[display] = control;

    // calculates initial step
    if (control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS))
    {
        control->step = 0;
        uint8_t i;
        control->scroll_dir = g_scroll_dir;

        for (i = 0; i < control->scale_points_count; i++)
        {
            if (control->value == control->scale_points[i]->value)
            {
                control->step = i;
            }
        }
        control->steps = control->scale_points_count;
    }
    else if (control->properties & FLAG_CONTROL_LOGARITHMIC)
    {
        if (control->minimum == 0.0)
            control->minimum = FLT_MIN;

        if (control->maximum == 0.0)
            control->maximum = FLT_MIN;

        if (control->value == 0.0)
            control->value = FLT_MIN;

        control->step =
            (control->steps - 1) * log(control->value / control->minimum) / log(control->maximum / control->minimum);
    }
    else if (control->properties & FLAG_CONTROL_INTEGER)
    {
        control->steps = (control->maximum - control->minimum) + 1;
        control->step =
            (control->value - control->minimum) / ((control->maximum - control->minimum) / control->steps);
    }
    else
    {
        control->step =
            (control->value - control->minimum) / ((control->maximum - control->minimum) / control->steps);        
    }

    // if tool is enabled don't draws the control
    if (display_has_tool_enabled(display)) return;

    // update the control screen
    screen_encoder(display, control);
}

// control removed from display
static void display_encoder_rm(uint8_t hw_id)
{
    uint8_t display;
    display = hw_id;

    if (hw_id > ENCODERS_COUNT) return;

    control_t *control = g_encoders[display];

    if (control)
    {
        data_free_control(control);
        g_encoders[display] = NULL;
        if (!display_has_tool_enabled(display))
            screen_encoder(display, NULL);
        return;
    }
    else if (!display_has_tool_enabled(display))
    {
        screen_encoder(display, NULL);
    }
}

// control assigned to display
static void display_pot_add(control_t *control)
{
    uint8_t id = control->hw_id - ENCODERS_COUNT - FOOTSWITCHES_ACTUATOR_COUNT;

    // checks if is already a control assigned in this display and remove it
    if (g_pots[id])
        data_free_control(g_pots[id]);

    // assign the new control
    g_pots[id] = control;

    //we check if the pot needs to be grabbed before. if so we raise the scroll_dir variable of the control
    //in this case scroll_dir is used as a flag. TODO: rename scroll_dir to actuator_flag
    uint32_t tmp_value = hardware_get_pot_value(id);

    if (tmp_value < g_pot_calibrations[0][id])
        tmp_value = g_pot_calibrations[0][id];
    else if (tmp_value > g_pot_calibrations[1][id])
        tmp_value = g_pot_calibrations[1][id];

    //since the actuall actuator limits can scale verry differently we scale the actuator value in a range of 50 to 3950
    //within this range the difference should be smaller then 50 before the pot starts turning
    uint32_t tmp_control_value = 0;

    if (g_pots[id]->properties & FLAG_CONTROL_LOGARITHMIC)
    {
    	//map the current value to the ADC range logarithmicly 
    	tmp_control_value = (g_pot_calibrations[1][id] - g_pot_calibrations[0][id]) * log(g_pots[id]->value / g_pots[id]->minimum) / log(g_pots[id]->maximum / g_pots[id]->minimum);
    }
    else 
    {
    	//map the current value to the ADC range
    	tmp_control_value = MAP(g_pots[id]->value, g_pots[id]->minimum,  g_pots[id]->maximum, g_pot_calibrations[0][id], g_pot_calibrations[1][id]);
    }

    if (g_lock_potentiometers)
    {   
        if  (g_pots[id]->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS))
        {
            g_pots[id]->scroll_dir = 1;
        }
        else
        {
            if ((tmp_value > tmp_control_value ? tmp_value - tmp_control_value : tmp_control_value - tmp_value) < POT_DIFF_THRESHOLD)
            {
                g_pots[id]->scroll_dir = 0;
            }
            else
            {
                g_pots[id]->scroll_dir = 1;
            }
        }
    }
    else g_pots[id]->scroll_dir = 0;

    // calculates initial step
    if (control->properties & FLAG_CONTROL_LOGARITHMIC)
    {
        if (control->minimum == 0.0)
            control->minimum = FLT_MIN;

        if (control->maximum == 0.0)
            control->maximum = FLT_MIN;

        if (control->value == 0.0)
            control->value = FLT_MIN;

        control->step =
            (control->steps - 1) * log(control->value / control->minimum) / log(control->maximum / control->minimum);
    }
    else
    {
        control->step =
            (control->value - control->minimum) / ((control->maximum - control->minimum) / control->steps);
    }

    // if tool is enabled don't draws the control
    if (display_has_tool_enabled(get_display_by_id(id, POTENTIOMETER)))
        return;

    // update the control screen
    screen_pot(id, control);
}

// control removed from display
static void display_pot_rm(uint8_t hw_id)
{
    uint8_t i;

    for (i = 0; i < POTS_COUNT; i++)
    {
        // if there is no controls assigned, load the default screen
        if (!g_pots[i] && !display_has_tool_enabled(get_display_by_id(i, POTENTIOMETER)))
        {
            screen_pot(i, NULL);
            continue;
        }

        // checks if the ids match
        if (hw_id == g_pots[i]->hw_id)
        {
            // remove the control
            data_free_control(g_pots[i]);
            g_pots[i] = NULL;

            // update the display
            if (!display_has_tool_enabled(get_display_by_id(i, POTENTIOMETER)))
                screen_pot(i, NULL);
        }
    }
}

static void set_alternated_led_list_colour(control_t *control)
{
    uint8_t color_id = control->scale_point_index % LED_LIST_AMOUNT_OF_COLORS;

    ledz_t *led = hardware_leds(control->hw_id - ENCODERS_COUNT);

    switch (color_id)
    {
        case 0:
            led->led_state.color = LED_LIST_COLOR_1;
        break;

        case 1:
            led->led_state.color = LED_LIST_COLOR_2;
        break;

        case 2:
            led->led_state.color = LED_LIST_COLOR_3;
        break;

        case 3:
            led->led_state.color = LED_LIST_COLOR_4;
        break;

        case 4:
            led->led_state.color = LED_LIST_COLOR_5;
        break;

        case 5:
            led->led_state.color = LED_LIST_COLOR_6;
        break;

        case 6:
            led->led_state.color = LED_LIST_COLOR_7;
        break;

        default:
            led->led_state.color = LED_LIST_COLOR_1;
        break;
    }

    ledz_set_state(led, LED_ON, LED_UPDATE);
}

// control assigned to foot
static void foot_control_add(control_t *control)
{
    uint8_t i;

    // checks the actuator id
    if (control->hw_id >= FOOTSWITCHES_ACTUATOR_COUNT + ENCODERS_COUNT)
        return;

    // checks if the foot is already used by other control and not is state updating
    if (g_foots[control->hw_id - ENCODERS_COUNT] && g_foots[control->hw_id - ENCODERS_COUNT] != control)
    {
        data_free_control(control);
        return;
    }

    // stores the foot
    g_foots[control->hw_id - ENCODERS_COUNT] = control;

    ledz_t *led = hardware_leds(control->hw_id - ENCODERS_COUNT);

    if (control->properties & FLAG_CONTROL_MOMENTARY)
    {
        if ((control->scroll_dir == 0)||(control->scroll_dir == 2))
            led->led_state.color = TRIGGER_COLOR;
        else
            led->led_state.color = TRIGGER_PRESSED_COLOR;
        
        if (!control->lock_led_actions)
            ledz_set_state(led, LED_ON, LED_UPDATE);

        // if is in tool mode break
        if ((!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT))) && !hardware_get_overlay_counter())
        {
            // updates the footer
            screen_footer(control->hw_id - ENCODERS_COUNT, control->label,
                         (control->value <= 0 ? TOGGLED_OFF_FOOTER_TEXT : TOGGLED_ON_FOOTER_TEXT), control->properties);
        }
    }
    else if (control->properties & FLAG_CONTROL_TRIGGER)
    {
        // trigger specification: http://lv2plug.in/ns/ext/port-props/#trigger
        if (!control->lock_led_actions) {
            // updates the led
            //check if its assigned to a trigger and if the button is release
            if (control->scroll_dir == 2)
                led->led_state.color = TRIGGER_COLOR;
            else
                led->led_state.color = TRIGGER_PRESSED_COLOR;

            // updates the led
            ledz_set_state(led, LED_ON, LED_UPDATE);
        }

        // if is in tool mode break
        if ((!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT))) && !hardware_get_overlay_counter())
        {
            // updates the footer (a getto fix here, the screen.c file did not regognize the NULL pointer so it did not allign the text properly, TODO fix this)
            screen_footer(control->hw_id - ENCODERS_COUNT, control->label, BYPASS_ON_FOOTER_TEXT, control->properties);
        }
    }
    else if (control->properties & FLAG_CONTROL_TOGGLED)
    {
        // toggled specification: http://lv2plug.in/ns/lv2core/#toggled

        if (!control->lock_led_actions) {
            // updates the led
            led->led_state.color = TOGGLED_COLOR;

            if (control->value <= 0)
                ledz_set_state(led, LED_OFF, LED_UPDATE);
            else
                ledz_set_state(led, LED_ON, LED_UPDATE);
        }

        // if is in tool mode break
        if ((!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT))) && !hardware_get_overlay_counter())
        {
            // updates the footer
            screen_footer(control->hw_id - ENCODERS_COUNT, control->label,
                         (control->value <= 0 ? TOGGLED_OFF_FOOTER_TEXT : TOGGLED_ON_FOOTER_TEXT), control->properties);
        }
    }
    else if (control->properties & FLAG_CONTROL_TAP_TEMPO)
    {
        // convert the time unit
        uint16_t time_ms = (uint16_t)(convert_to_ms(control->unit, control->value) + 0.5);

        if (!control->lock_led_actions) {
            led->led_state.color = TAP_TEMPO_COLOR;
            led->led_state.state = LED_BLINK;
            led->led_state.amount_of_blinks = LED_BLINK_INFINIT;

            // setup the led blink
            if (time_ms > TAP_TEMPO_TIME_ON) {
                led->led_state.time_on = TAP_TEMPO_TIME_ON;
                led->led_state.time_off = time_ms - TAP_TEMPO_TIME_ON;
            }
            else {
                led->led_state.time_on = time_ms / 2;
                led->led_state.time_off = time_ms / 2;
            }

            ledz_set_state(led, LED_BLINK, LED_UPDATE);
        }

        // calculates the maximum tap tempo value
        if (g_tap_tempo[control->hw_id - ENCODERS_COUNT].state == TT_INIT)
        {
            uint32_t max;

            // time unit (ms, s)
            if (strcmp(control->unit, "ms") == 0 || strcmp(control->unit, "s") == 0)
            {
                max = (uint32_t)(convert_to_ms(control->unit, control->maximum) + 0.5);
                //makes sure we enforce a proper timeout
                if (max > TAP_TEMPO_DEFAULT_TIMEOUT)
                    max = TAP_TEMPO_DEFAULT_TIMEOUT;
            }
            // frequency unit (bpm, Hz)
            else
            {
                //prevent division by 0 case
                if (control->minimum == 0)
                    max = TAP_TEMPO_DEFAULT_TIMEOUT;
                else
                    max = (uint32_t)(convert_to_ms(control->unit, control->minimum) + 0.5);

                //makes sure we enforce a proper timeout
                if (max > TAP_TEMPO_DEFAULT_TIMEOUT)
                    max = TAP_TEMPO_DEFAULT_TIMEOUT;
            }

            g_tap_tempo[control->hw_id - ENCODERS_COUNT].max = max;
            g_tap_tempo[control->hw_id - ENCODERS_COUNT].state = TT_COUNTING;
        }

        // if is in tool mode break
        if ((!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT))) && !hardware_get_overlay_counter())
        {
            // footer text composition
            char value_txt[32];

            //if unit=ms or unit=bpm -> use 0 decimal points
            if (strcasecmp(control->unit, "ms") == 0 || strcasecmp(control->unit, "bpm") == 0)
                i = int_to_str(control->value, value_txt, sizeof(value_txt), 0);
            //if unit=s or unit=hz or unit=something else-> use 2 decimal points
            else
                i = float_to_str(control->value, value_txt, sizeof(value_txt), 2);

            //add space to footer
            value_txt[i++] = ' ';
            strcpy(&value_txt[i], control->unit);

            // updates the footer
            screen_footer(control->hw_id - ENCODERS_COUNT, control->label, value_txt, control->properties);
        }
    }
    else if (control->properties & FLAG_CONTROL_BYPASS)
    {
        if (!control->lock_led_actions) {
            // updates the led
            led->led_state.color = BYPASS_COLOR;

            if (control->value <= 0)
                ledz_set_state(led, LED_ON, LED_UPDATE);
            else
                ledz_set_state(led, LED_OFF, LED_UPDATE);
        }

        // if is in tool mode break
        if ((!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT))) && !hardware_get_overlay_counter())
        {
            // updates the footer
            screen_footer(control->hw_id - ENCODERS_COUNT, control->label,
                         (control->value ? BYPASS_ON_FOOTER_TEXT : BYPASS_OFF_FOOTER_TEXT), control->properties);
        }
    }
    else if (control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS))
    {
        if (!control->lock_led_actions) {
            // updates the led
            //check if its assigned to a trigger and if the button is released
            if ((control->scroll_dir == 2) || (control->scale_points_flag & FLAG_SCALEPOINT_ALT_LED_COLOR)) {
                if (control->scale_points_flag & FLAG_SCALEPOINT_ALT_LED_COLOR) {
                    set_alternated_led_list_colour(control);
                }
                else {
                    led->led_state.color = ENUMERATED_COLOR;
                    ledz_set_state(led, LED_ON, LED_UPDATE);
                }
            }
            else {
                led->led_state.color = ENUMERATED_PRESSED_COLOR;
                ledz_set_state(led, LED_ON, LED_UPDATE);
            }
        }

        // locates the current value
        control->step = 0;
        for (i = 0; i < control->scale_points_count; i++)
        {
            if (control->value == control->scale_points[i]->value)
            {
                control->step = i;
                break;
            }
        }
        control->steps = control->scale_points_count;

        // if is in tool mode break
        if ((!display_has_tool_enabled(get_display_by_id(i, FOOT))) && !hardware_get_overlay_counter())
        {
            // updates the footer
            screen_footer(control->hw_id - ENCODERS_COUNT, control->label, control->scale_points[i]->label, control->properties);
        }
    }
}

// control removed from foot
static void foot_control_rm(uint8_t hw_id)
{
    uint8_t i;

    //if (hw_id < ENCODERS_COUNT) return;

    for (i = 0; i < FOOTSWITCHES_ACTUATOR_COUNT; i++)
    {
        // if there is no controls assigned, load the default screen
        if (!g_foots[i])
        {
            if (!display_has_tool_enabled(i))
                screen_footer(i, NULL, NULL, 0);
            continue;
        }

        // checks if effect_instance and symbol match
        if (hw_id == g_foots[i]->hw_id)
        {
            // remove the control
            data_free_control(g_foots[i]);
            g_foots[i] = NULL;

            // turn off the led
            ledz_t *led = hardware_leds(i);
            led->led_state.color = MAX_COLOR_ID;
            ledz_set_state(led, LED_OFF, LED_UPDATE);

            // check if foot isn't being used to bank function
            // update the footer
            if (!display_has_tool_enabled(i))
                screen_footer(i, NULL, NULL, 0);
        }
    }
}

static void parse_control_page(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;

    control_t *control = data_parse_control(&list[1]);

    naveg_add_control(control, 0);
}

static void request_control_page(control_t *control, uint8_t dir)
{
    // sets the response callback
    ui_comm_webgui_set_response_cb(parse_control_page, NULL);

    char buffer[40];
    uint8_t i;

    i = copy_command(buffer, CMD_CONTROL_PAGE); 

    // insert the hw_id on buffer
    i += int_to_str(control->hw_id, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    //bitmask for the page direction and wrap around
    uint8_t bitmask = 0;
    if (dir) bitmask |= FLAG_PAGINATION_PAGE_UP;
    if ((control->hw_id >= ENCODERS_COUNT) && (control->scale_points_flag & FLAG_SCALEPOINT_WRAP_AROUND)) bitmask |= FLAG_PAGINATION_WRAP_AROUND;

    // insert the direction on buffer
    i += int_to_str(bitmask, &buffer[i], sizeof(buffer) - i, 0);

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the banks list be received
    ui_comm_webgui_wait_response();
}

static void parse_banks_list(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t count = strarr_length(&list[5]);

    // workaround freeze when opening menu
    delay_ms(20);

    // free the current banks list
    if (g_banks) data_free_banks_list(g_banks);

    // parses the list
    g_banks = data_parse_banks_list(&list[5], count);

    if (g_banks)
    {
        g_banks->menu_max = (atoi(list[2]));
        g_banks->page_min = (atoi(list[3]));
        g_banks->page_max = (atoi(list[4]));
    }

    naveg_set_banks(g_banks);
}

//only toggled from the naveg toggle tool function
static void request_banks_list(uint8_t dir)
{
    g_bp_state = BANKS_LIST;

    // sets the response callback
    ui_comm_webgui_set_response_cb(parse_banks_list, NULL);

    char buffer[40];
    memset(buffer, 0, 20);
    uint8_t i;

    i = copy_command(buffer, CMD_BANKS); 

    // insert the direction on buffer
    i += int_to_str(dir, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    //insert current bank, because first time we are entering the menu
    i += int_to_str(g_current_bank, &buffer[i], sizeof(buffer) - i, 0);

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the banks list be received
    ui_comm_webgui_wait_response();

    g_banks->hover = g_current_bank;
    g_banks->selected = g_current_bank;
}

//requested from the bp_up / bp_down functions when we reach the end of a page
static void request_next_bank_page(uint8_t dir)
{
    //we need to save our current hover and selected here, the parsing function will discard those
    uint8_t prev_hover = g_banks->hover;
    uint8_t prev_selected = g_banks->selected;

    //request the banks as usual
    g_bp_state = BANKS_LIST;

    // sets the response callback
    ui_comm_webgui_set_response_cb(parse_banks_list, NULL);

    char buffer[40];
    memset(buffer, 0, sizeof buffer);
    uint8_t i;

    i = copy_command(buffer, CMD_BANKS); 

    // insert the direction on buffer
    i += int_to_str(dir, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    i += int_to_str(g_banks->hover, &buffer[i], sizeof(buffer) - i, 0);

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the banks list be received
    ui_comm_webgui_wait_response();

    //restore our previous hover / selected bank
    g_banks->hover = prev_hover;
    g_banks->selected = prev_selected;
}

//called from the request functions and the naveg_initail_state
static void parse_pedalboards_list(void *data, menu_item_t *item)
{
    (void) item;
    char **list = data;
    uint32_t count = strarr_length(&list[5]);

    // workaround freeze when opening menu
    delay_ms(20);

    // free the navigation pedalboads list
    if (g_naveg_pedalboards)
        data_free_pedalboards_list(g_naveg_pedalboards);

    // parses the list
    g_naveg_pedalboards = data_parse_pedalboards_list(&list[5], count);

    if (g_naveg_pedalboards)
    {
        g_naveg_pedalboards->menu_max = (atoi(list[2]));
        g_naveg_pedalboards->page_min = (atoi(list[3]));
        g_naveg_pedalboards->page_max = (atoi(list[4])); 
    }
}

//requested when clicked on a back
static void request_pedalboards(uint8_t dir, uint16_t bank_uid)
{
    uint8_t i = 0;
    char buffer[40];
    memset(buffer, 0, sizeof buffer);

    // sets the response callback
    ui_comm_webgui_set_response_cb(parse_pedalboards_list, NULL);

    i = copy_command((char *)buffer, CMD_PEDALBOARDS);

    uint8_t bitmask = 0;
    if (dir == 1) {bitmask |= FLAG_PAGINATION_PAGE_UP;}
    else if (dir == 2) bitmask |= FLAG_PAGINATION_INITIAL_REQ;

    // insert the direction on buffer
    i += int_to_str(bitmask, &buffer[i], sizeof(buffer) - i, 0);

    // inserts one space
    buffer[i++] = ' ';

    if ((dir == 2) && (g_current_bank != g_banks->hover))
    {
        // start at pb 0
        i += int_to_str(0, &buffer[i], sizeof(buffer) - i, 0);
    } 
    else
    {
        // insert the current hover on buffer
        i += int_to_str(g_naveg_pedalboards->hover - 1, &buffer[i], sizeof(buffer) - i, 0);
    }

    // inserts one space
    buffer[i++] = ' ';

    // copy the bank uid
    i += int_to_str((bank_uid), &buffer[i], sizeof(buffer) - i, 0);

    buffer[i++] = 0;

    uint16_t prev_hover = g_current_pedalboard;
    uint16_t prev_selected = g_current_pedalboard;

    if (g_naveg_pedalboards)
    {
        prev_hover = g_naveg_pedalboards->hover;
        prev_selected = g_naveg_pedalboards->selected;
    }

    // sends the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the pedalboards list be received
    ui_comm_webgui_wait_response();

    if (g_naveg_pedalboards)
    {
        g_naveg_pedalboards->hover = prev_hover;
        g_naveg_pedalboards->selected = prev_selected;
    }
}

static void send_load_pedalboard(uint16_t bank_id, const char *pedalboard_uid)
{
    uint16_t i;
    char buffer[40];
    memset(buffer, 0, sizeof buffer);

    i = copy_command((char *)buffer, CMD_PEDALBOARD_LOAD);

    // copy the bank id
    i += int_to_str(bank_id, &buffer[i], 8, 0);

    // inserts one space
    buffer[i++] = ' ';

    const char *p = pedalboard_uid;
    // copy the pedalboard uidf
    if (!*p) 
    {
        buffer[i++] = '0';
    }
    else
    {
        while (*p)
        {
            buffer[i++] = *p;
            p++;
        }
    }
    buffer[i] = 0;

    // sets the response callback
    ui_comm_webgui_set_response_cb(NULL, NULL);

    // send the data to GUI
    ui_comm_webgui_send(buffer, i);

    // waits the pedalboard loaded message to be received
    ui_comm_webgui_wait_response();
}

static void control_set(uint8_t id, control_t *control)
{
    uint32_t now, delta;

    if (control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS)
         && !(control->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_TRIGGER | FLAG_CONTROL_MOMENTARY)))
    {
        if (control->hw_id < ENCODERS_COUNT)
        {
            // update the screen
            if (!display_has_tool_enabled(id))
                screen_encoder(id, control);
        }
        else if ((ENCODERS_COUNT <= control->hw_id) && ( control->hw_id < FOOTSWITCHES_ACTUATOR_COUNT + ENCODERS_COUNT))
        {
            uint8_t trigger_led_change = 0;
            if (!control->lock_led_actions) {
                ledz_t *led = hardware_leds(control->hw_id - ENCODERS_COUNT);

                // updates the led
                //check if its assigned to a trigger and if the button is released
                if ((control->scroll_dir == 2) || (control->scale_points_flag & FLAG_SCALEPOINT_ALT_LED_COLOR)) {
                    if (control->scale_points_flag & FLAG_SCALEPOINT_ALT_LED_COLOR) {
                        trigger_led_change = 1;
                    }
                    else {
                        led->led_state.color = ENUMERATED_COLOR;
                        ledz_set_state(led, LED_ON, LED_UPDATE);
                    }
                }
                else {
                    led->led_state.color = ENUMERATED_PRESSED_COLOR;
                    ledz_set_state(led, LED_ON, LED_UPDATE);
                }
            }

            if (!(control->properties & FLAG_CONTROL_REVERSE))
            {
                // increments the step
                if (control->step < (control->steps - 1))
                {
                    control->step++;
                    control->scale_point_index++;
                }
                //if we are reaching the end of the control
                else if (control->scale_points_flag & FLAG_SCALEPOINT_END_PAGE)
                {
                    //we wrap around so the step becomes 0 again
                    if (control->scale_points_flag & FLAG_SCALEPOINT_WRAP_AROUND)
                    {
                    	if (control->scale_points_flag & FLAG_SCALEPOINT_PAGINATED)
                    	{
                        	request_control_page(control, 1);
                        	return;
                    	}
                    	else
                    	{
                    		control->step = 0;
                    	    control->scale_point_index = 0;
                        }
                    }
                    //we are at max and dont wrap around
                    else return; 
                }
                //we need to request a new page
                else if (control->scale_points_flag & FLAG_SCALEPOINT_PAGINATED) 
                {
                    //request new data, a new control we be assigned after
                    request_control_page(control, 1);

                    //since a new control is assigned we can return
                    return;
                }
                else return;
            }
            else 
            {
                // decrements the step
                if (control->step > 0)
                {
                    control->step--;
                    control->scale_point_index--;
                }
                //we are at the end of our list ask for more data
                else if (control->scale_points_flag & (FLAG_SCALEPOINT_PAGINATED | FLAG_SCALEPOINT_WRAP_AROUND))
                {
                    //request new data, a new control we be assigned after
                    request_control_page(control, 0);
    
                    //since a new control is assigned we can return
                    return;
                }
                else return;
            }

            // updates the value and the screen
            control->value = control->scale_points[control->step]->value;
            if (!display_has_tool_enabled(get_display_by_id(id, FOOT)))
                screen_footer(control->hw_id - ENCODERS_COUNT, control->label, control->scale_points[control->step]->label, control->properties);

            if (trigger_led_change == 1)
                set_alternated_led_list_colour(control);
        }
    }
    else if (control->properties & FLAG_CONTROL_TRIGGER)
    {
        control->value = control->maximum;
        // to update the footer and screen
        foot_control_add(control);

        if (!control->scroll_dir) return;
    }
    else if (control->properties & FLAG_CONTROL_MOMENTARY)
    {
        //not the cleanest, will be redone in 1.12
        if (control->properties & FLAG_CONTROL_BYPASS) {
            if (control->properties & FLAG_CONTROL_REVERSE)
                control->value = control->scroll_dir;
            else
                control->value = 1 - control->scroll_dir;
        }
        else {
            if (control->properties & FLAG_CONTROL_REVERSE)
                control->value = 1 - control->scroll_dir;
            else
                control->value = control->scroll_dir;
        }

        // to update the footer and screen
        foot_control_add(control);
    }
    else if (control->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS))
    {
        if (control->hw_id < ENCODERS_COUNT)
        {
            // update the screen
            if (!display_has_tool_enabled(id))
                screen_encoder(id, control);
        }
        else if ( (ENCODERS_COUNT + FOOTSWITCHES_ACTUATOR_COUNT <= control->hw_id) && (control->hw_id < TOTAL_ACTUATORS))
        {
            if (!display_has_tool_enabled(get_display_by_id(id, POTENTIOMETER)))
            {
                screen_pot(id, control);
            }
        }
        else
        {
            if (control->value > control->minimum) control->value = control->minimum;
            else control->value = control->maximum;

            // to update the footer and screen
            foot_control_add(control);
        }
    }
    else if (control->properties & FLAG_CONTROL_TAP_TEMPO)
    {
        now = hardware_timestamp();
        delta = now - g_tap_tempo[control->hw_id - ENCODERS_COUNT].time;
        g_tap_tempo[control->hw_id - ENCODERS_COUNT].time = now;

        if (g_tap_tempo[control->hw_id - ENCODERS_COUNT].state == TT_COUNTING)
        {
            // checks if delta almost suits maximum allowed value
            if ((delta > g_tap_tempo[control->hw_id - ENCODERS_COUNT].max) &&
                ((delta - TAP_TEMPO_MAXVAL_OVERFLOW) < g_tap_tempo[control->hw_id - ENCODERS_COUNT].max))
            {
                // sets delta to maxvalue if just slightly over, instead of doing nothing
                delta = g_tap_tempo[control->hw_id - ENCODERS_COUNT].max;
            }

            // checks the tap tempo timeout
            if (delta <= g_tap_tempo[control->hw_id - ENCODERS_COUNT].max)
            {
                //get current value of tap tempo in ms
                float currentTapVal = convert_to_ms(control->unit, control->value);
                //check if it should be added to running average
                if (fabs(currentTapVal - delta) < TAP_TEMPO_TAP_HYSTERESIS)
                {
                    // converts and update the tap tempo value
                    control->value = (2*(control->value) + convert_from_ms(control->unit, delta)) / 3;
                }
                else
                {
                    // converts and update the tap tempo value
                    control->value = convert_from_ms(control->unit, delta);
                }

                // checks the values bounds
                if (control->value > control->maximum) control->value = control->maximum;
                if (control->value < control->minimum) control->value = control->minimum;

                // updates the foot
                foot_control_add(control);
            }
        }
    }
    //lin, log, int
    else
    {
        if (control->hw_id < ENCODERS_COUNT)
        {
            // update the screen
            if (!display_has_tool_enabled(id))
                screen_encoder(id, control);
        }
        else if ( (ENCODERS_COUNT + FOOTSWITCHES_ACTUATOR_COUNT <= control->hw_id) && (control->hw_id < TOTAL_ACTUATORS))
        {
            if (!display_has_tool_enabled(get_display_by_id(id, POTENTIOMETER)))
            {
                screen_pot(id, control);
            }
        }
    }

    char buffer[128];
    uint8_t i;

    i = copy_command(buffer, CMD_CONTROL_SET);

    // insert the hw_id on buffer
    i += int_to_str(control->hw_id, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i++] = ' ';

    // insert the value on buffer
    i += float_to_str(control->value, &buffer[i], sizeof(buffer) - i, 6);
    buffer[i] = 0;

    if (g_self_test_mode) {
        ui_comm_webgui_clear_tx_buffer();
    }

    // send the data to GUI
    ui_comm_webgui_send(buffer, i);

    //wait for a response from mod-ui
    if (!g_self_test_mode) {
        ui_comm_webgui_wait_response();
    }
}

static void bp_enter(void)
{
    const char *title;

    if (naveg_ui_status())
    {
        tool_off(DISPLAY_TOOL_NAVIG);
        tool_on(DISPLAY_TOOL_SYSTEM, 0);
        tool_on(DISPLAY_TOOL_SYSTEM_SUBMENU, 1);
        screen_system_menu(g_current_item);
    }

    if (!g_banks)
        return;

    if (g_bp_state == BANKS_LIST)
    {
        //make sure that we request the right page if we have a selected pedalboard
        if (g_current_bank == g_banks->hover)
            g_naveg_pedalboards->hover = g_current_pedalboard;

        //index is relevent in our array so - page_min
        request_pedalboards(PAGE_DIR_INIT, atoi(g_banks->uids[g_banks->hover - g_banks->page_min]));

        // if reach here, received the pedalboards list
        g_bp_state = PEDALBOARD_LIST;
        //index is relevent in our array so - page_min
        title = g_banks->names[g_banks->hover - g_banks->page_min];
        g_bp_first = 1;
    }
    else if (g_bp_state == PEDALBOARD_LIST)
    {
        // checks if is the first option (back to banks list)
        if (g_naveg_pedalboards->hover == 0)
        {
            g_bp_state = BANKS_LIST;
            title = "BANKS";
        }
        else
        {
            g_bp_first=0;

            // request to GUI load the pedalboard
            //index is relevant in the array so - page_min, also the HMI array is always shifted right 1 because of back to banks, correct here
            //TODO dirty fix for the 0'th index item not being properly saved in the UID array. Please FIXME later 
            if (g_naveg_pedalboards->hover == 1) send_load_pedalboard(atoi(g_banks->uids[g_banks->hover - g_banks->page_min]), "0");
            else send_load_pedalboard(atoi(g_banks->uids[g_banks->hover - g_banks->page_min]), g_naveg_pedalboards->uids[(g_naveg_pedalboards->hover - g_naveg_pedalboards->page_min - 1)]);

            g_current_pedalboard = g_naveg_pedalboards->hover;

            // stores the current bank and pedalboard
            g_banks->selected = g_banks->hover;
            g_current_bank = g_banks->selected;

            // sets the variables to update the screen
            title = g_banks->names[g_banks->hover - g_banks->page_min];
        }
    }
    else
    {
        return;
    }

    //check if we need to display the selected item or out of bounds
    if (g_current_bank == g_banks->hover)
    {
        g_naveg_pedalboards->selected = g_current_pedalboard;
        g_naveg_pedalboards->hover = g_current_pedalboard;
    }
    else 
    {
        g_naveg_pedalboards->selected = g_naveg_pedalboards->menu_max + 1;
        g_naveg_pedalboards->hover = 0;
    }

    screen_bp_list(title, (g_bp_state == PEDALBOARD_LIST)? g_naveg_pedalboards : g_banks);
}

static void bp_up(void)
{
    bp_list_t *bp_list = 0;
    const char *title = 0;

    if (!g_banks) return;

    if (g_bp_state == BANKS_LIST)
    {
        //if we are nearing the final 3 items of the page, and we already have the end of the page in memory, or if we just need to go down
        if(g_banks->page_min == 0) 
        {
            //check if we are not already at the end
            if (g_banks->hover == 0) return;
            else 
            {
                //we still have banks in our list
                g_banks->hover--;

                bp_list = g_banks;
                title = "BANKS";
            }
        }
        //are we comming from the bottom of the menu?
        else
        {
            //we always keep 3 items in front of us, if not request new page
            if (g_banks->hover <= (g_banks->page_min + 3))
            {
                g_banks->hover--;
                title = "BANKS";

                //request new page
                request_next_bank_page(PAGE_DIR_DOWN);
            
                bp_list = g_banks;
            }   
            //we have items, just go up
            else 
            {
                g_banks->hover--;
                bp_list = g_banks;
                title = "BANKS";
            }
        }
    }
    else if (g_bp_state == PEDALBOARD_LIST)
    {
        //are we reaching the bottom of the menu?
        if(g_naveg_pedalboards->page_min == 0) 
        {
            //check if we are not already at the end
            if (g_naveg_pedalboards->hover == 0) return;
            else 
            {
                //go down till the end
                //we still have items in our list
                g_naveg_pedalboards->hover--;
                bp_list = g_naveg_pedalboards;
                title = g_banks->names[g_banks->hover - g_banks->page_min];
            }
        }
        else
        {
            //we always keep 3 items in front of us, if not request new page, we add 4 because the bottom item is always "> back to banks" and we dont show that here
            if (g_naveg_pedalboards->hover <= (g_naveg_pedalboards->page_min + 5))
            {
                g_naveg_pedalboards->hover--;
                title = g_banks->names[g_banks->hover - g_banks->page_min];
                //request new page
                request_pedalboards(PAGE_DIR_DOWN, atoi(g_banks->uids[g_banks->hover - g_banks->page_min]));
            
                bp_list = g_naveg_pedalboards;
            }   
            //we have items, just go up
            else 
            {
                g_naveg_pedalboards->hover--;
                bp_list = g_naveg_pedalboards;
                title = g_banks->names[g_banks->hover - g_banks->page_min];
            }
        }
    }
    else return;

    screen_bp_list(title, bp_list);
}

static void bp_down(void)
{
    bp_list_t *bp_list = 0;
    const char *title = 0;

    if (!g_banks) return;

    if (g_bp_state == BANKS_LIST)
    {
        //are we reaching the bottom of the menu?
        if(g_banks->page_max == g_banks->menu_max) 
        {
            //check if we are not already at the end
            if (g_banks->hover >= g_banks->menu_max -1) return;
            else 
            {
                //we still have banks in our list
                g_banks->hover++;
                bp_list = g_banks;
                title = "BANKS";
            }
        }
        else 
        {
            //we always keep 3 items in front of us, if not request new page
            if (g_banks->hover >= (g_banks->page_max - 4))
            {
                g_banks->hover++;
                title = "BANKS";
                //request new page
                request_next_bank_page(PAGE_DIR_UP);

                bp_list = g_banks;
            }   
            //we have items, just go down
            else 
            {
                g_banks->hover++;
                bp_list = g_banks;
                title = "BANKS";
            }
        }
    }
    else if (g_bp_state == PEDALBOARD_LIST)
    {
        //are we reaching the bottom of the menu, -1 because menu max is bigger then page_max
        if(g_naveg_pedalboards->page_max == g_naveg_pedalboards->menu_max) 
        {
            //check if we are not already at the end, we dont need so substract by one, since the "> back to banks" item is added on parsing
            if (g_naveg_pedalboards->hover == g_naveg_pedalboards->menu_max) return;
            else 
            {
                //we still have items in our list
                g_naveg_pedalboards->hover++;
                bp_list = g_naveg_pedalboards;
                title = g_banks->names[g_banks->hover - g_banks->page_min];
            }
        }
        else 
        {
            //we always keep 3 items in front of us, if not request new page, we need to substract by 5, since the "> back to banks" item is added on parsing 
            if (g_naveg_pedalboards->hover >= (g_naveg_pedalboards->page_max - 4))
            {
                title = g_banks->names[g_banks->hover - g_banks->page_min];
                //request new page
                request_pedalboards(PAGE_DIR_UP, atoi(g_banks->uids[g_banks->hover - g_banks->page_min]));

                g_naveg_pedalboards->hover++;
                bp_list = g_naveg_pedalboards;
            }   
            //we have items, just go down
            else 
            {
                g_naveg_pedalboards->hover++;
                bp_list = g_naveg_pedalboards;
                title = g_banks->names[g_banks->hover - g_banks->page_min];
            }
        }
    }
    else return;

    screen_bp_list(title, bp_list);
}

static void menu_enter(uint8_t display_id)
{

    uint8_t i;
    node_t *node = (display_id || g_dialog_active) ? g_current_menu : g_current_main_menu;
    menu_item_t *item = (display_id || g_dialog_active) ? g_current_item : g_current_main_item;

    //TODO Handle better, when menu is going to be redesigned
    static uint8_t value_toggle = 0;

    if (item->desc->type == MENU_LIST || item->desc->type == MENU_SELECT)
    {
        // locates the clicked item
        node = display_id ? g_current_menu->first_child : g_current_main_menu->first_child;
        for (i = 0; i < item->data.hover; i++) node = node->next;

        // gets the menu item
        item = node->data;
        // checks if is 'back to previous'
        if (item->desc->type == MENU_RETURN)
        {
            if (item->desc->action_cb)
                item->desc->action_cb(item, MENU_EV_ENTER);

            node = node->parent->parent;
            item = node->data;
        }
        //extra check if we need to switch back to non-ui connected mode on the current-pb and banks menu
        else if ((item->desc->id == BANKS_ID) && (!naveg_ui_status()))
        {
            item->desc->type = MENU_NONE;
        }

        // updates the current item
       	if ((item->desc->type != MENU_TOGGLE) && (item->desc->type != MENU_NONE)) g_current_item = node->data;
       	// updates these 3 specific toggle items (toggle items with pop-ups)
        if (item->desc->parent_id == PROFILES_ID || item->desc->id == EXP_CV_INP || item->desc->id == EXP_MODE || item->desc->id == HP_CV_OUTP) g_current_item = node->data;
    }
    else if (item->desc->type == MENU_CONFIRM || item->desc->type == MENU_CANCEL || item->desc->type == MENU_OK ||
    		item->desc->parent_id == PROFILES_ID || item->desc->id == EXP_CV_INP || item->desc->id == EXP_MODE || item->desc->id == HP_CV_OUTP)
    {
        // calls the action callback
        if ((item->desc->type != MENU_OK) && (item->desc->type != MENU_CANCEL) && (item->desc->action_cb))
            item->desc->action_cb(item, MENU_EV_ENTER);

    	if (item->desc->id == BANKS_ID)
    	{
        	if (naveg_ui_status())
        	{
                //change menu type to menu_ok to display pop-up
            	item->desc->type = MENU_MESSAGE;
            	g_current_item = item;
        	}
        	else
        	{
                //reset the menu type to its original state
        		item->desc->type = MENU_NONE;
        		g_current_item = item;
    		}
    	}
    	else
    	{
        	// gets the menu item
        	item = display_id ? g_current_menu->data : g_current_main_menu->data;
        	g_current_item = item;
        }
    }

    //if the tuner is clicked either enable it or action_cb
    if (item->desc->id == TUNER_ID)
    {
        if (!tool_is_on(DISPLAY_TOOL_TUNER))
        {
            naveg_toggle_tool(DISPLAY_TOOL_TUNER, 1);
            return;
        }
        // calls the action callback
        else if (g_current_main_item->desc->action_cb) g_current_main_item->desc->action_cb(g_current_main_item, MENU_EV_ENTER);
    }

    // FIXME: that's dirty, so dirty...
    if (item->desc->id == BANKS_ID)
    {
        if (naveg_ui_status()) item->desc->type = MENU_MESSAGE;
    }

    //check if we are entering a volume menu, we need to get the value before printing items in the list
    if ((item->desc->id == INP_ID) || (item->desc->id == OUTP_ID))
    {
        if (item->desc->action_cb)
            item->desc->action_cb(item, MENU_EV_ENTER);
    }

    // checks the selected item
    if (item->desc->type == MENU_LIST || item->desc->type == MENU_SELECT)
    {
        // changes the current menu
        g_current_menu = node;

        // initialize the counter
        item->data.list_count = 0;

        // adds the menu lines
        for (node = node->first_child; node; node = node->next)
        {
            menu_item_t *item_child = node->data;

            //all the menu items that have a value that needs to be updated when enterign the menu
            if ((item_child->desc->type == MENU_SET) || (item_child->desc->type == MENU_TOGGLE) || (item_child->desc->type == MENU_VOL) ||
            	(item_child->desc->id == TEMPO_ID) || (item_child->desc->id == TUNER_ID) || (item_child->desc->id == BYPASS_ID) )
                {
                    //update the value with menu_ev_none
                   if (item_child->desc->action_cb) item_child->desc->action_cb(item_child, MENU_EV_NONE);
                }
            item->data.list[item->data.list_count++] = item_child->name;
        }

        //prevent toggling of these items.
        if ((item->desc->id != TEMPO_ID) && (item->desc->id != BYPASS_ID))
        {
            // calls the action callback
            if ((item->desc->action_cb)) item->desc->action_cb(item, MENU_EV_ENTER);
        }
    }
    else if (item->desc->type == MENU_CONFIRM ||item->desc->type == MENU_OK || item->desc->parent_id == PROFILES_ID || item->desc->id == EXP_MODE || item->desc->id == EXP_CV_INP || item->desc->id == HP_CV_OUTP || item->desc->type == MENU_MESSAGE)
    {
        if (item->desc->id == USB_B_MODE_ID)
        {
            item->desc->action_cb(item, MENU_EV_ENTER);
            item->desc->type = MENU_SET;
            value_toggle = 0;
        }
        else if (item->desc->type == MENU_OK)
        {
            //if bleutooth activate right away
            if (item->desc->id == BLUETOOTH_DISCO_ID)
            {
                item->desc->action_cb(item, MENU_EV_ENTER);
            }

            // highlights the default button
            item->data.hover = 0;

            // defines the buttons count
            item->data.list_count = 1;
        }
        else if (item->desc->type == MENU_MESSAGE)
        {
            // highlights the default button
            item->data.hover = 0;

            // defines the buttons count
            item->data.list_count = 0;
        }
        else
        {
            // highlights the default button
            item->data.hover = 1;

            // defines the buttons count
            item->data.list_count = 2;
        }

        // default popup content value
        item->data.popup_content = NULL;
        // locates the popup menu
        i = 0;
        while (g_menu_popups[i].popup_content)
        {
            if (item->desc->id == g_menu_popups[i].menu_id)
            {
                //load/reload profile popups
                if ((item->desc->parent_id == PROFILES_ID) && (item->desc->id != PROFILES_ID+5))
                {
                    //set the 'reload' profile popups
                    if (item->data.value)
                    {
                        item->data.popup_content = g_menu_popups[i + 5].popup_content;
                        item->data.popup_header = g_menu_popups[i + 5].popup_header;
                    }
                    //set the 'load' profile popups
                    else
                    {
                        item->data.popup_content = g_menu_popups[i].popup_content;
                        item->data.popup_header = g_menu_popups[i].popup_header;
                    }
                }
                //save profile popups
                else if (item->desc->id == PROFILES_ID+5)
                {
                    item->data.popup_content = g_menu_popups[i].popup_content;

                    //add the to be saved profile char to the header of the popup
                    char *profile_char;

                    item->data.value = system_get_current_profile();

                    if (item->data.value ==1) profile_char = "A";
                    else if (item->data.value ==2) profile_char = "B";
                    else if (item->data.value ==3) profile_char = "C";
                    else if (item->data.value ==4) profile_char = "D";
                    else profile_char = "X";

                    char *txt =  g_menu_popups[i].popup_header;
                    char * str3 = (char *) malloc(1 + strlen(txt)+ strlen(profile_char) );
                    strcpy(str3, txt);
                    strcat(str3, profile_char);
                    item->data.popup_header = (str3);

                }
                //cv toggle popups, they have a different popup depending on their current value
                else if ((item->desc->id == EXP_CV_INP) || (item->desc->id == HP_CV_OUTP))
                {
                    if (!item->data.value)
                    {
                        item->data.popup_content = g_menu_popups[i].popup_content;
                        item->data.popup_header = g_menu_popups[i].popup_header;
                    }
                    else
                    {
                        item->data.popup_content = g_menu_popups[i + 1].popup_content;
                        item->data.popup_header = g_menu_popups[i + 1].popup_header;
                    }
                }
                //other popups
                else
                {
                    item->data.popup_content = g_menu_popups[i].popup_content;
                    item->data.popup_header = g_menu_popups[i].popup_header;
                }

                break;
            }
            i++;
        }
    }
    else if (item->desc->type == MENU_TOGGLE)
    {
        // calls the action callback
        if (item->desc->action_cb) item->desc->action_cb(item, MENU_EV_ENTER);
    }
    else if (item->desc->type == MENU_CANCEL || item->desc->type == MENU_OK)
    {
        // highlights the default button
        item->data.hover = 0;

        // defines the buttons count
        item->data.list_count = 1;

        // default popup content value
        item->data.popup_content = NULL;

        // locates the popup menu
        i = 0;
        while (g_menu_popups[i].popup_content)
        {
            if (item->desc->id == g_menu_popups[i].menu_id)
            {
                item->data.popup_content = g_menu_popups[i].popup_content;
                break;
            }
            i++;
        }

        // calls the action callback
        if ((item->desc->action_cb) && (item->desc->id != BANKS_ID))
        {
            item->desc->action_cb(item, MENU_EV_ENTER);
        }
    }
    else if (item->desc->type == MENU_NONE)
    {
        // checks if the parent item type is MENU_SELECT
        if (g_current_item->desc->type == MENU_SELECT)
        {
            // deselects all items
            for (i = 0; i < g_current_item->data.list_count; i++)
                deselect_item(g_current_item->data.list[i]);

            // selects the current item
            select_item(item->name);
        }

        // calls the action callback
        if (item->desc->action_cb)
            item->desc->action_cb(item, MENU_EV_ENTER);

        if (display_id)
        {
            item = g_current_menu->data;
            g_current_item = item;
        }
    }
    else if ((item->desc->type == MENU_VOL) || (item->desc->type == MENU_SET))
    {
        if (item->desc->id == USB_B_MODE_ID) {
            if (value_toggle == 0) {
                value_toggle = 1;
            }
            else {
                if (g_reboot_value != (int)item->data.value) {
                    //now do popup
                    item->desc->type = MENU_CONFIRM;

                    item->data.popup_content = NULL;

                    // highlights the default button
                    item->data.hover = 1;

                    // defines the buttons count
                    item->data.list_count = 2;

                    // locates the popup menu
                    i = 0;
                    while (g_menu_popups[i].popup_content)
                    {
                        if (item->desc->id == g_menu_popups[i].menu_id)
                        {
                            item->data.popup_content = g_menu_popups[i].popup_content;
                            item->data.popup_header = g_menu_popups[i].popup_header;
                        }

                        i++;
                    }
                }
                else {
                    //resets the menu node
                    item->desc->type = MENU_SET;
                    item = g_current_menu->data;
                    g_current_item = item;
                    value_toggle = 0;
                }
            }
        }

        else if (display_id)
        {
            static uint8_t toggle = 0;
            if (toggle == 0)
            {
                toggle = 1;
                // calls the action callback
                if ((item->desc->action_cb) && (item->desc->type != MENU_SET))
                    item->desc->action_cb(item, MENU_EV_ENTER);
            }
            else
            {
                toggle = 0;
                if (item->desc->type == MENU_VOL)
                    system_save_gains_cb(item, MENU_EV_ENTER);
                else
                    item->desc->action_cb(item, MENU_EV_ENTER);
                //resets the menu node
                item = g_current_menu->data;
                g_current_item = item;
            }
        }
    }

    //if we entered calibration mode we dont do these steps
    if (g_calibration_mode) return;

    if (item->desc->parent_id == DEVICE_ID && item->desc->action_cb)
        item->desc->action_cb(item, MENU_EV_ENTER);

    if (tool_is_on(DISPLAY_TOOL_SYSTEM) && !tool_is_on(DISPLAY_TOOL_NAVIG))
    {
        screen_system_menu(item);
        g_update_cb = NULL;
        g_update_data = NULL;
        if (item->desc->need_update)
        {
            g_update_cb = item->desc->action_cb;
            g_update_data = item;
        }
    }

    if (item->desc->type == MENU_CONFIRM2)
    {
        g_dialog_active = false;
        portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(g_dialog_sem, &xHigherPriorityTaskWoken);
    }
}

static void menu_up(uint8_t display_id)
{
    menu_item_t *item = (display_id) ? g_current_item : g_current_main_item;

    if (item->desc->type == MENU_SET)
    {
        //substract one, if we reach the limit, value becomes the limit
        if ((item->data.value -= (item->data.step)) < item->data.min) {
            item->data.value = item->data.min;
        }
    }
    else if (item->desc->type != MENU_VOL)
    {
        if (item->data.hover > 0) {
            item->data.hover--;
        }
    }

    if (item->desc->action_cb)
        item->desc->action_cb(item, MENU_EV_UP);

    screen_system_menu(item);
}


static void menu_down(uint8_t display_id)
{
    menu_item_t *item = (display_id) ? g_current_item : g_current_main_item;

    if (item->desc->type == MENU_SET)
    {
        //up one, if we reach the limit, value becomes the limit
        if ((item->data.value += (item->data.step)) > item->data.max) {
            item->data.value = item->data.max;
        }
    }
    else if (item->desc->type != MENU_VOL)
    {
        if (item->data.hover < (item->data.list_count - 1))
            item->data.hover++;
    }

    if (item->desc->action_cb)
        item->desc->action_cb(item, MENU_EV_DOWN);

    screen_system_menu(item);
}

static void tuner_enter(void)
{
    static uint8_t input = 1;

    char buffer[128];
    uint32_t i = copy_command(buffer, CMD_TUNER_INPUT);

    // toggle the input
    input = (input == 1 ? 2 : 1);

    // inserts the input number
    i += int_to_str(input, &buffer[i], sizeof(buffer) - i, 0);
    buffer[i] = 0;

    // send the data to GUI
    ui_comm_webgui_send(buffer, i);

    // updates the screen
    screen_tuner_input(input);
}

static void create_menu_tree(node_t *parent, const menu_desc_t *desc)
{
    uint8_t i;
    menu_item_t *item;

    for (i = 0; g_menu_desc[i].name; i++)
    {
        if (desc->id == g_menu_desc[i].parent_id)
        {
            item = (menu_item_t *) MALLOC(sizeof(menu_item_t));
            item->data.hover = 0;
            item->data.selected = 0xFF;
            item->data.list_count = 0;
            item->data.list = (char **) MALLOC(g_max_items_list * sizeof(char *));
            item->desc = &g_menu_desc[i];
            item->name = MALLOC(MAX_CHARS_MENU_NAME);
            strcpy(item->name, g_menu_desc[i].name);

            node_t *node;
            node = node_child(parent, item);

            if (item->desc->type == MENU_LIST || item->desc->type == MENU_SELECT)
                create_menu_tree(node, &g_menu_desc[i]);
        }
    }
}

node_t *get_menu_node_by_ID(uint8_t menu_id)
{
    node_t *node = g_menu->first_child->first_child;

    //make sure we have all menu value's updated
    node_t *child_nodes, *grandchild_nodes;
    menu_item_t *item_child, *item_grandchild;

    while(node)
    {
        item_child = node->data;
        if (item_child->desc->id == menu_id)
        {
            return node;
        }

        if (node->first_child)
        {
            child_nodes = node->first_child;
            while(child_nodes)
            {
                item_child = child_nodes->data;
                if (item_child->desc->id == menu_id)
                {
                    return child_nodes;
                }

                if (child_nodes->first_child)
                {
                    grandchild_nodes = child_nodes->first_child;
                    while(grandchild_nodes)
                    {
                        item_grandchild = grandchild_nodes->data;
                        if (item_grandchild->desc->id == menu_id)
                        {
                            return grandchild_nodes;
                        }

                        grandchild_nodes = grandchild_nodes->next;
                    }
                }

                child_nodes = child_nodes->next;
            }
        }

        node = node->next;
    }

    return NULL;
}

static void reset_menu_hover(node_t *menu_node)
{
    node_t *node;
    for (node = menu_node->first_child; node; node = node->next)
    {
        menu_item_t *item = node->data;
        if (item->desc->type == MENU_LIST || item->desc->type == MENU_SELECT)
            item->data.hover = 0;
        reset_menu_hover(node);
    }
}

/*
************************************************************************************************************************
*           GLOBAL FUNCTIONS
************************************************************************************************************************
*/

void naveg_init(void)
{
    uint32_t i;

    // initialize the global variables
    for (i = 0; i < ENCODERS_COUNT; i++)
    {
        // initialize the display controls pointers
        g_encoders[i] = NULL;
    }
    for (i = 0; i < FOOTSWITCHES_ACTUATOR_COUNT; i++)
    {
        // initialize the foot controls pointers
        g_foots[i] = NULL;

        // initialize the tap tempo
        g_tap_tempo[i].state = TT_INIT;
    }
    for (i = 0; i < POTS_COUNT; i++)
    {
        // initialize the display controls pointers
        g_pots[i] = NULL;

        //get the calibration for this pot
        g_pot_calibrations[0][i] = calibration_get_min(i);
        g_pot_calibrations[1][i] = calibration_get_max(i);
    }

    g_banks = NULL;
    g_naveg_pedalboards = NULL;
    g_current_pedalboard = 1;
    g_bp_state = BANKS_LIST;

    // initializes the bank functions
    for (i = 0; i < BANK_FUNC_COUNT; i++)
    {
        g_bank_functions[i].function = BANK_FUNC_NONE;
        g_bank_functions[i].hw_id = 0xFF;
    }

    // counts the maximum items amount in menu lists
    uint8_t count = 0;
    for (i = 0; g_menu_desc[i].name; i++)
    {
        uint8_t j = i + 1;
        for (; g_menu_desc[j].name; j++)
        {
            if (g_menu_desc[i].id == g_menu_desc[j].parent_id)
                count++;
        }

        if (count > g_max_items_list)
            g_max_items_list = count;
        count = 0;
    }

    // adds one to line 'back to previous menu'
    g_max_items_list++;

    // creates the menu tree (recursively)
    const menu_desc_t root_desc = {"root", MENU_LIST, -1, -1, NULL, 0};
    g_menu = node_create(NULL);
    create_menu_tree(g_menu, &root_desc);

    // sets current menu
    g_current_menu = g_menu;
    g_current_item = g_menu->first_child->data;

    // initialize update variables
    g_update_cb = NULL;
    g_update_data = NULL;

    g_initialized = 1;

    //check if we need to hide actuators or not
    system_hide_actuator_cb(NULL, MENU_EV_NONE);
    
    //check if we need to lock the potentiometers or not
    system_lock_pots_cb(NULL, MENU_EV_NONE);

    //check the page mode
    system_page_mode_cb(NULL, MENU_EV_NONE);

    vSemaphoreCreateBinary(g_dialog_sem);
    // vSemaphoreCreateBinary is created as available which makes
    // first xSemaphoreTake pass even if semaphore has not been given
    // http://sourceforge.net/p/freertos/discussion/382005/thread/04bfabb9
    xSemaphoreTake(g_dialog_sem, 0);
}

uint8_t naveg_get_actuator_type(uint8_t hw_id)
{
    if (hw_id < ENCODERS_COUNT)
        return ACT_ENCODER;
    else if (hw_id < ENCODERS_COUNT + FOOTSWITCHES_ACTUATOR_COUNT)
        return ACT_FOOTSWITCH;
    else
        return ACT_POT;
}

void naveg_initial_state(uint16_t max_menu, uint16_t page_min, uint16_t page_max, char *bank_uid, char *pedalboard_uid, char **pedalboards_list)
{
    if (!pedalboards_list)
    {
        if (g_banks)
        {
            g_banks->selected = 0;
            g_banks->hover = 0;
            g_banks->page_min = 0;
            g_banks->page_max = 0;
            g_banks->menu_max = 0;
            g_current_bank = 0;
        }

        if (g_naveg_pedalboards)
        {
            g_naveg_pedalboards->selected = 0;
            g_naveg_pedalboards->hover = 0;
            g_naveg_pedalboards->page_min = 0;
            g_naveg_pedalboards->page_max = 0;
            g_naveg_pedalboards->menu_max = 0;
            g_current_pedalboard = 0;
        }

        return;
    }

    // sets the bank index
    uint16_t bank_id = atoi(bank_uid);
    g_current_bank = bank_id;
    if (g_banks)
    {
        g_banks->selected = bank_id;
        g_banks->hover = bank_id;
    }

    // checks and free the navigation pedalboads list
    if (g_naveg_pedalboards) data_free_pedalboards_list(g_naveg_pedalboards);

    // parses the list
    g_naveg_pedalboards = data_parse_pedalboards_list(pedalboards_list, strarr_length(pedalboards_list));

    if (!g_naveg_pedalboards) return;

    g_naveg_pedalboards->page_min = page_min;
    g_naveg_pedalboards->page_max = page_max;
    g_naveg_pedalboards->menu_max = max_menu;

    g_current_pedalboard = atoi(pedalboard_uid) + 1;

    g_naveg_pedalboards->hover = g_current_pedalboard;
    g_naveg_pedalboards->selected = g_current_pedalboard;
}

void naveg_ui_connection(uint8_t status)
{
    if (!g_initialized) return;

    if (status == UI_CONNECTED)
    {
        g_ui_connected = 1;
    }
    else
    {
        g_ui_connected = 0;

        // reset the banks and pedalboards state after return from ui connection
        if (g_banks)
            data_free_banks_list(g_banks);
        if (g_naveg_pedalboards)
            data_free_pedalboards_list(g_naveg_pedalboards);
        g_banks = NULL;
        g_naveg_pedalboards = NULL;
    }


    if ((tool_is_on(DISPLAY_TOOL_NAVIG)) || tool_is_on(DISPLAY_TOOL_SYSTEM))
    {
        naveg_toggle_tool(DISPLAY_TOOL_SYSTEM, 0);
        node_t *node = g_current_main_menu;

		//sets the pedalboard items back to original
		for (node = node->first_child; node; node = node->next)
		{
 			// gets the menu item
        	menu_item_t *item = node->data;

        	if (item->desc->id == BANKS_ID)
    		{
        		item->desc->type = MENU_NONE;
        		g_current_item = item;
    		}
		}
    }

}

void naveg_add_control(control_t *control, uint8_t protocol)
{
    if (!g_initialized) return;
    if (!control) return;

    // first tries remove the control
    naveg_remove_control(control->hw_id);

    if (control->hw_id < ENCODERS_COUNT)
    {
        display_encoder_add(control);
    }
    else if (control->hw_id < (ENCODERS_COUNT + FOOTSWITCHES_ACTUATOR_COUNT))
    {
        if (protocol) control->scroll_dir = 2;
        else control->scroll_dir = 0;

        foot_control_add(control);
    }
    else
    {
        display_pot_add(control);
    }
}

void naveg_remove_control(uint8_t hw_id)
{
    if (!g_initialized) return;

    if (hw_id < ENCODERS_COUNT)
    {
        display_encoder_rm(hw_id);
    }
    else if (hw_id < ENCODERS_COUNT + FOOTSWITCHES_ACTUATOR_COUNT)
    {
        foot_control_rm(hw_id);
    }
    else
    {
        display_pot_rm(hw_id);
    }
}

void naveg_inc_control(uint8_t display)
{
    if (!g_initialized) return;

    // if is in tool or selftest mode return
    if ((display_has_tool_enabled(display)) || g_self_test_mode) return;

    control_t *control = g_encoders[display];
    if (!control) return;

    if  ((control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE)))
    {
    	//check/sets the direction
		if (control->scroll_dir == 0)
		{
		    control->scroll_dir = 1;
		    control_set(display, control);
		    return;
		}

        if (control->scale_points_flag & FLAG_SCALEPOINT_PAGINATED)
        {
        	// increments the step
        	if (control->step < (control->steps - 2))
        	    control->step++;
        	else
        	{
        	    //request new data, a new control we be assigned after
        	    request_control_page(control, 1);

            	//since a new control is assigned we can return
            	return;
        	}
    	}
    	else 
    	{
    		// increments the step
    		if (control->step < (control->steps - 1))
    		    control->step++;
    		else
    		    return;	
    	}
    }
    else if (control->properties & FLAG_CONTROL_TOGGLED)
    {
        if (control->value == 1)
            return;
        else 
            control->value = 1;
    }
    else if (control->properties & FLAG_CONTROL_BYPASS)
    {
        if (control->value == 0)
            return;
        else 
            control->value = 0;
    }
    else
    {
        // increments the step
        if (control->step < (control->steps - 1))
            control->step++;
        else
            return;
    }
    // converts the step to absolute value
    step_to_value(control);

    // applies the control value
    control_set(display, control);
}

void naveg_dec_control(uint8_t display)
{
    if (!g_initialized) return;

    // if is in tool or selftest mode return
    if ((display_has_tool_enabled(display)) || g_self_test_mode) return;

    control_t *control = g_encoders[display];
    if (!control) return;

    if  ((control->properties & (FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE)))
    {
		//check/sets the direction
		if (control->scroll_dir != 0)
		{
		    control->scroll_dir = 0;
		    control_set(display, control);
		    return;
		}

        if (control->scale_points_flag & FLAG_SCALEPOINT_PAGINATED)
        {

        	// decrements the step
        	if (control->step > 1)
        	    control->step--;
        	else
        	{
        		//temporaraly change to add the right direction on parsing the new page
        		g_scroll_dir = 0;

            	//request new data, a new control we be assigned after
            	request_control_page(control, 0);

            	//restore so we can add controls normaly again
            	g_scroll_dir = 1;

            	//since a new control is assigned we can return
            	return;
        	}
        }
        else 
    	{
            // decrements the step
            if (control->step > 0)
                control->step--;
            else
                return;
    	}
    }
    else if (control->properties & FLAG_CONTROL_TOGGLED)
    {
        if (control->value == 0)
            return;
        else 
            control->value = 0;
    }
    else if (control->properties & FLAG_CONTROL_BYPASS)
    {
        if (control->value == 1)
            return;
        else 
            control->value = 1;
    }
    else
    {
        // decrements the step
        if (control->step > 0)
            control->step--;
        else
            return;
    }
    // converts the step to absolute value
    step_to_value(control);

    // applies the control value
    control_set(display, control);
}

void naveg_set_control(uint8_t hw_id, float value)
{
    if (!g_initialized) return;

    uint8_t id;
    control_t *control;

    //encoder
    if (hw_id < ENCODERS_COUNT)
    {
        control = g_encoders[hw_id];
        id = hw_id;
    }
    //button
    else if (hw_id < FOOTSWITCHES_ACTUATOR_COUNT + ENCODERS_COUNT)
    {
        control = g_foots[hw_id - ENCODERS_COUNT];
        id = hw_id - ENCODERS_COUNT;
    }
    //potentiometer
    else
    {
        control = g_pots[hw_id - FOOTSWITCHES_ACTUATOR_COUNT - ENCODERS_COUNT];
        id = hw_id -FOOTSWITCHES_ACTUATOR_COUNT -ENCODERS_COUNT;
    }

    if (control)
    {
        control->value = value;
        if (value < control->minimum)
            control->value = control->minimum;
        if (value > control->maximum)
            control->value = control->maximum;

        // updates the step value
        control->step =
            (control->value - control->minimum) / ((control->maximum - control->minimum) / control->steps);


        //encoder
        if (hw_id < ENCODERS_COUNT)
        {
            if (!display_has_tool_enabled(hw_id))
            {
                screen_encoder(id, control);
            }
        }
        //button
        else if (hw_id < FOOTSWITCHES_ACTUATOR_COUNT + ENCODERS_COUNT)
        {
            uint8_t i;
            if (control->properties & FLAG_CONTROL_MOMENTARY)
            {
                // updates the footer
                    screen_footer(control->hw_id - ENCODERS_COUNT, control->label,
                                 (control->value <= 0 ? TOGGLED_OFF_FOOTER_TEXT : TOGGLED_ON_FOOTER_TEXT), control->properties);
            }
            // trigger specification: http://lv2plug.in/ns/ext/port-props/#trigger
            else if (control->properties & FLAG_CONTROL_TRIGGER)
            {
                // updates the led
                ledz_t *led = hardware_leds(control->hw_id - ENCODERS_COUNT);

                //check if its assigned to a trigger and if the button is released
                if (!control->scroll_dir) {
                    led->led_state.color = TRIGGER_COLOR;
                    ledz_set_state(led, LED_ON, LED_UPDATE);
                    return;
                }
                else if (control->scroll_dir == 2) {
                    led->led_state.color = TRIGGER_COLOR;
                    ledz_set_state(led, LED_ON, LED_UPDATE);
                }
                else {
                    led->led_state.color = TRIGGER_PRESSED_COLOR;
                    ledz_set_state(led, LED_ON, LED_UPDATE);
                }

                // if is in tool mode break
                if (!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT)))
                {
                    // updates the footer (a getto fix here, the screen.c file did not regognize the NULL pointer so it did not allign the text properly, TODO fix this)
                    screen_footer(control->hw_id - ENCODERS_COUNT, control->label, BYPASS_ON_FOOTER_TEXT, control->properties);
                }
            }
            // toggled specification: http://lv2plug.in/ns/lv2core/#toggled
            else if (control->properties & FLAG_CONTROL_TOGGLED)
            {
                // updates the led
                ledz_t *led = hardware_leds(control->hw_id - ENCODERS_COUNT);
                led->led_state.color = TOGGLED_COLOR;

                if (control->value <= 0)
                    ledz_set_state(led, LED_OFF, LED_UPDATE);
                else
                    ledz_set_state(led, LED_ON, LED_UPDATE);

                // if is in tool mode break
                if (!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT)))
                {
                    // updates the footer
                    screen_footer(control->hw_id - ENCODERS_COUNT, control->label,
                                 (control->value <= 0 ? TOGGLED_OFF_FOOTER_TEXT : TOGGLED_ON_FOOTER_TEXT), control->properties);
                }
            }
            else if (control->properties & FLAG_CONTROL_TAP_TEMPO)
            {
                // convert the time unit
                uint16_t time_ms = (uint16_t)(convert_to_ms(control->unit, control->value) + 0.5);

                // setup the led blink
                ledz_t *led = hardware_leds(control->hw_id - ENCODERS_COUNT);
                led->led_state.color = TAP_TEMPO_COLOR;
                led->led_state.state = LED_BLINK;
                led->led_state.amount_of_blinks = LED_BLINK_INFINIT;

                // setup the led blink
                if (time_ms > TAP_TEMPO_TIME_ON) {
                    led->led_state.time_on = TAP_TEMPO_TIME_ON;
                    led->led_state.time_off = time_ms - TAP_TEMPO_TIME_ON;
                }
                else {
                    led->led_state.time_on = time_ms / 2;
                    led->led_state.time_off = time_ms / 2;
                }

                ledz_set_state(led, LED_BLINK, LED_UPDATE);

                // calculates the maximum tap tempo value
                if (g_tap_tempo[control->hw_id - ENCODERS_COUNT].state == TT_INIT)
                {
                    uint32_t max;

                    // time unit (ms, s)
                    if (strcmp(control->unit, "ms") == 0 || strcmp(control->unit, "s") == 0)
                    {
                        max = (uint32_t)(convert_to_ms(control->unit, control->maximum) + 0.5);
                        //makes sure we enforce a proper timeout
                        if (max > TAP_TEMPO_DEFAULT_TIMEOUT)
                            max = TAP_TEMPO_DEFAULT_TIMEOUT;
                    }
                    // frequency unit (bpm, Hz)
                    else
                    {
                        //prevent division by 0 case
                        if (control->minimum == 0)
                            max = TAP_TEMPO_DEFAULT_TIMEOUT;
                        else
                            max = (uint32_t)(convert_to_ms(control->unit, control->minimum) + 0.5);
                        //makes sure we enforce a proper timeout
                        if (max > TAP_TEMPO_DEFAULT_TIMEOUT)
                            max = TAP_TEMPO_DEFAULT_TIMEOUT;
                    }

                    g_tap_tempo[control->hw_id - ENCODERS_COUNT].max = max;
                    g_tap_tempo[control->hw_id - ENCODERS_COUNT].state = TT_COUNTING;
                }

                // if is in tool mode break
                if (!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT)))
                {
                    // footer text composition
                    char value_txt[32];

                    //if unit=ms or unit=bpm -> use 0 decimal points
                    if (strcasecmp(control->unit, "ms") == 0 || strcasecmp(control->unit, "bpm") == 0)
                        i = int_to_str(control->value, value_txt, sizeof(value_txt), 0);
                    //if unit=s or unit=hz or unit=something else-> use 2 decimal points
                    else
                        i = float_to_str(control->value, value_txt, sizeof(value_txt), 2);

                    //add space to footer
                    value_txt[i++] = ' ';
                    strcpy(&value_txt[i], control->unit);

                    // updates the footer
                    screen_footer(control->hw_id - ENCODERS_COUNT, control->label, value_txt, control->properties);
                }
            }
            else if (control->properties & FLAG_CONTROL_BYPASS)
            {
                // updates the led
                ledz_t *led = hardware_leds(control->hw_id - ENCODERS_COUNT);
                led->led_state.color = BYPASS_COLOR;

                if (control->value <= 0)
                    ledz_set_state(led, LED_ON, LED_UPDATE);
                else
                    ledz_set_state(led, LED_OFF, LED_UPDATE);

                // if is in tool mode break
                if (!display_has_tool_enabled(get_display_by_id(control->hw_id - ENCODERS_COUNT, FOOT)))
                {
                    // updates the footer
                    screen_footer(control->hw_id - ENCODERS_COUNT, control->label,
                                 (control->value ? BYPASS_ON_FOOTER_TEXT : BYPASS_OFF_FOOTER_TEXT), control->properties);
                }
            }
            else if (control->properties & (FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION | FLAG_CONTROL_SCALE_POINTS))
            {
                // updates the led
                if (control->scale_points_flag & FLAG_SCALEPOINT_ALT_LED_COLOR) {
                    set_alternated_led_list_colour(control);
                }
                else {
                    ledz_t *led = hardware_leds(control->hw_id - ENCODERS_COUNT);
                    led->led_state.color = ENUMERATED_COLOR;
                    ledz_set_state(led, LED_ON, LED_UPDATE);
                }

                // locates the current value
                control->step = 0;
                for (i = 0; i < control->scale_points_count; i++)
                {
                    if (control->value == control->scale_points[i]->value)
                    {
                        control->step = i;
                        break;
                    }
                }
                control->steps = control->scale_points_count;

                // if is in tool mode break
                if (!display_has_tool_enabled(get_display_by_id(i, FOOT)))
                {
                    // updates the footer
                    screen_footer(control->hw_id - ENCODERS_COUNT, control->label, control->scale_points[i]->label, control->properties);
                }
            }
        }
        //potentiometer
        else
        {
            uint16_t tmp_value = hardware_get_pot_value(id);
            
            if (tmp_value < g_pot_calibrations[0][id])
                tmp_value = g_pot_calibrations[0][id];
            else if (tmp_value > g_pot_calibrations[1][id])
                tmp_value = g_pot_calibrations[1][id];

            uint16_t tmp_control_value = 0;

            if (g_pots[id]->properties & FLAG_CONTROL_LOGARITHMIC)
            {
            	//map the current value to the ADC range logarithmicly 
            	tmp_control_value = (g_pot_calibrations[1][id] - g_pot_calibrations[0][id]) * log(g_pots[id]->value / g_pots[id]->minimum) / log(g_pots[id]->maximum / g_pots[id]->minimum);
            }
            else 
            {
            	//map the current value to the ADC range
            	tmp_control_value = MAP(g_pots[id]->value, g_pots[id]->minimum,  g_pots[id]->maximum, g_pot_calibrations[0][id], g_pot_calibrations[1][id]);
            }

            if (g_lock_potentiometers)
            {   
                if  (g_pots[id]->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS))
                {
                    g_pots[id]->scroll_dir = 1;
                }
                else
                {
                    if ((tmp_value > tmp_control_value ? tmp_value - tmp_control_value : tmp_control_value - tmp_value) < POT_DIFF_THRESHOLD)
                    {
                        g_pots[id]->scroll_dir = 0;
                    }
                    else
                    {
                        g_pots[id]->scroll_dir = 1;
                    }
                }
            }
            else g_pots[id]->scroll_dir = 0;

            if (!display_has_tool_enabled(get_display_by_id(id, POTENTIOMETER)))
            {
                screen_pot(id, control);
            }
        }

    }
}

float naveg_get_control_value(uint8_t hw_id)
{
    if (!g_initialized) return 0.0;
    control_t *control;

    control = search_encoder(hw_id);
    if (control) return control->value;

    return 0.0;
}

control_t* naveg_get_control(uint8_t hw_id)
{
    if (!g_initialized) return NULL;
    control_t *control = NULL;

    if (hw_id < ENCODERS_COUNT)
        control = search_encoder(hw_id);
    else if (hw_id < ENCODERS_COUNT + FOOTSWITCHES_ACTUATOR_COUNT)
        control = g_foots[hw_id - ENCODERS_COUNT];
    else if (hw_id < ENCODERS_COUNT + FOOTSWITCHES_ACTUATOR_COUNT + POTS_COUNT)
        control = g_pots[hw_id - ENCODERS_COUNT - FOOTSWITCHES_ACTUATOR_COUNT];

    if (control) return control;

    return NULL;
}

void naveg_pot_change(uint8_t pot)
{
    if (!g_initialized) return;

    // checks the pot id
    if (pot >= POTS_COUNT) return;

    // checks if there is assigned control
    if (g_pots[pot] == NULL) return;

    //if we are in tool mode block potentiometers
    if (display_has_tool_enabled(get_display_by_id(pot, POTENTIOMETER))) return;

    //set the new value as tmp
    float tmp_value = hardware_get_pot_value(pot);

    float tmp_control_value;

    if (g_pots[pot]->properties & FLAG_CONTROL_LOGARITHMIC)
    {
    	//map the current value to the ADC range logarithmicly 
    	tmp_control_value = (g_pot_calibrations[1][pot] - g_pot_calibrations[0][pot]) * log(g_pots[pot]->value / g_pots[pot]->minimum) / log(g_pots[pot]->maximum / g_pots[pot]->minimum);
    }
    //default, liniar
    else 
    {
    	//map the current value to the ADC range
    	tmp_control_value = MAP(g_pots[pot]->value, g_pots[pot]->minimum,  g_pots[pot]->maximum, g_pot_calibrations[0][pot], g_pot_calibrations[1][pot]);
    }

    //if the actuator is still locked
    if ((g_pots[pot]->scroll_dir == 1) && !g_self_test_mode)
    {
        if  (g_pots[pot]->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS))
        {
            uint16_t half_way = (g_pot_calibrations[1][pot] - g_pot_calibrations[0][pot]) /2;
            if ((tmp_value >= (half_way - 100)) && (tmp_value <= (half_way + 100)))
            {
                g_pots[pot]->scroll_dir = 0;
            }
            else
            {
                g_pots[pot]->scroll_dir = 1;
                return; 
            }
        }
        else
        {
            if ((tmp_value > tmp_control_value ? tmp_value - tmp_control_value : tmp_control_value - tmp_value) < POT_DIFF_THRESHOLD)
            {
                g_pots[pot]->scroll_dir = 0;
            }
            else
            {
                g_pots[pot]->scroll_dir = 1;
                return;
            }
        }
    }

    if (g_pots[pot]->properties & FLAG_CONTROL_INTEGER)
    {
        float value = MAP(tmp_value, (float)g_pot_calibrations[0][pot], (float)g_pot_calibrations[1][pot],  g_pots[pot]->minimum,  g_pots[pot]->maximum);
        g_pots[pot]->value = round(value);
    }
    else if (g_pots[pot]->properties & FLAG_CONTROL_LOGARITHMIC)
    {
        //n = current ADC value (linear range value)
        //x = ADC min = g_pot_calibrations[0][pot]
        //y = ADC max = g_pot_calibrations[1][pot]
        //p = log min = g_pots[pot]->minimum
        //q = log max = g_pots[pot]->maximum
        //to transpose from a linear range to another log range we can use: 
        // log_value = 10 ^ (log(p) + ((n - x)/(y - x) * (log(q) - log(p))) ))
        g_pots[pot]->value = pow(10, log10(g_pots[pot]->minimum)  + (((float)tmp_value - (float)g_pot_calibrations[0][pot]) / ((float)g_pot_calibrations[1][pot] - (float)g_pot_calibrations[0][pot])) * (log10(g_pots[pot]->maximum) - log10(g_pots[pot]->minimum)) );
    }
    //toggles
    else if (g_pots[pot]->properties & (FLAG_CONTROL_TOGGLED | FLAG_CONTROL_BYPASS))
    {
        uint16_t half_way = (g_pot_calibrations[1][pot] - g_pot_calibrations[0][pot]) /2;
        if (tmp_value >= half_way)
        {
            g_pots[pot]->value = (g_pots[pot]->properties & FLAG_CONTROL_TOGGLED)?1:0;
        } 
        else 
        {
            g_pots[pot]->value = (g_pots[pot]->properties & FLAG_CONTROL_TOGGLED)?0:1;
        }
    }
    //default, liniar
    else
    {
    	g_pots[pot]->value = MAP(tmp_value, (float)g_pot_calibrations[0][pot], (float)g_pot_calibrations[1][pot],  g_pots[pot]->minimum,  g_pots[pot]->maximum);
    }

    //catch any inacuracy's in for example log calculation. 
    if (g_pots[pot]->value < g_pots[pot]->minimum) g_pots[pot]->value = g_pots[pot]->minimum;
    if (g_pots[pot]->value > g_pots[pot]->maximum) g_pots[pot]->value = g_pots[pot]->maximum;

   	// send the pot value
   	control_set(pot, g_pots[pot]);
}

void naveg_foot_change(uint8_t foot, uint8_t pressed)
{
    if (!g_initialized) return;

    //if in selftest mode, we just send if we are working or not
    if ((g_self_test_mode) && !g_dialog_active)
    {
        char buffer[30];
        uint8_t i;
        //skip control action
        if (g_self_test_cancel_button && (foot == 5))
        {
            i = copy_command(buffer, CMD_SELFTEST_SKIP_CONTROL);
        }
        else 
        {
            i = copy_command(buffer, CMD_SELFTEST_BUTTON_CLICKED);

            // insert the hw_id on buffer
            i += int_to_str(foot, &buffer[i], sizeof(buffer) - i, 0);
        }

        ui_comm_webgui_clear_tx_buffer();

        // send the data to GUI
        ui_comm_webgui_send(buffer, i);

        return;
    }

    // checks the foot id
    if (foot >= FOOTSWITCHES_COUNT) return;

    if (display_has_tool_enabled(get_display_by_id(foot, FOOT))) return;

    switch (foot)
    {
        //actuator buttons
        case 0:
        case 1:
        case 2:
        case 3:

            // checks if there is assigned control
            if (g_foots[foot] == NULL) return;

           	//detect a release action 
 			if (!pressed)
            {
                if (!g_foots[foot]->lock_led_actions) {
                    //check if we use the release action for this actuator
                    if (g_foots[foot]->properties & (FLAG_CONTROL_MOMENTARY | FLAG_CONTROL_TRIGGER)) {
                        ledz_t *led = hardware_leds(g_foots[foot]->hw_id - ENCODERS_COUNT);
                        led->led_state.color = TRIGGER_COLOR;
                        ledz_set_state(led, LED_ON, LED_UPDATE);
                    }
                    else if (g_foots[foot]->properties & (FLAG_CONTROL_SCALE_POINTS | FLAG_CONTROL_REVERSE | FLAG_CONTROL_ENUMERATION)) {
                            if (g_foots[foot]->scale_points_flag & FLAG_SCALEPOINT_ALT_LED_COLOR) {
                                set_alternated_led_list_colour(g_foots[foot]);
                            }
                            else {
                                ledz_t *led = hardware_leds(g_foots[foot]->hw_id - ENCODERS_COUNT);
                                led->led_state.color = ENUMERATED_COLOR;
                                ledz_set_state(led, LED_ON, LED_UPDATE);
                            }
                        break;
                    }
                }

                //not used right now anymore, maybe in the future, TODO: rename to actuator flag
                g_foots[foot]->scroll_dir = pressed;

                //we dont actually preform an action here
                if (!(g_foots[foot]->properties & FLAG_CONTROL_MOMENTARY))
                    return;
            }

            g_foots[foot]->scroll_dir = pressed;

            control_set(foot, g_foots[foot]);
        break;

        //snapshot buttons
        case 4:
        case 6:

            if (!g_page_mode)
            {
                if (snapshot_loaded[foot - FOOTSWITCHES_ACTUATOR_COUNT] > 0)
                {
                	if (pressed == 1)
                	{
                        ledz_t *led = hardware_leds(foot);
                        led->led_state.color = SNAPSHOT_LOAD_COLOR;
                        ledz_set_state(led, LED_ON, LED_UPDATE);
                    }
                    else
                   	{
                        if ( snapshot_loaded[foot - FOOTSWITCHES_ACTUATOR_COUNT] == 1)
                    	{
                            ledz_t *led = hardware_leds(foot);
                            led->led_state.color = SNAPSHOT_COLOR;
                            ledz_set_state(led, LED_ON, LED_UPDATE);

                            char buffer[10];
                            uint8_t i;

                            i = copy_command(buffer, CMD_DUOX_SNAPSHOT_LOAD);

                            i += int_to_str(foot - FOOTSWITCHES_ACTUATOR_COUNT, &buffer[i], sizeof(buffer) - i, 0);

                            //do not do actuators here
                            reset_queue();

                            ui_comm_webgui_send(buffer, i);
                            if (!g_self_test_mode) {
                                ui_comm_webgui_wait_response();
                            }
                        }
                        else
                        {
                            snapshot_loaded[foot - FOOTSWITCHES_ACTUATOR_COUNT] = 1;
                        }
            	    }
                }
            }
            //up and down pages mode
            else 
            {
                if (!pressed) return;

                char buffer[10];
                uint8_t i;
                int8_t j;
                i = copy_command(buffer, CMD_NEXT_PAGE);
                uint8_t pagefound = 0;

                //up
                if (foot == 6)
                {
                    j = page;
                    while (!pagefound)
                    {
                        j++;
                        if (j >= PAGES_COUNT)
                        {
                            j = 0;
                        }

                        //we went in a loop, only one page
                        if (j == page)
                        {
                            break;
                        }

                        //page found
                        if (page_available[j] == 1)
                        {
                            page = j;
                            pagefound = 1;
                            break;
                        }
                    }
                }

                //down
                else
                {
                    j = page;
                    while (!pagefound)
                    {
                        j--;
                        if (j < 0)
                        {
                            j = PAGES_COUNT-1;
                        }

                        //we went in a loop, only one page
                        if (j == page)
                        {
                            break;
                        }

                        //page found
                        if (page_available[j] == 1)
                        {
                            page = j;
                            pagefound = 1;
                            break;
                        }
                    }
                }

                if (!pagefound)
                    return;

                i += int_to_str(page, &buffer[i], sizeof(buffer) - i, 0);

                //clear controls            
                uint8_t q;
                for (q = 0; q < TOTAL_ACTUATORS; q++)
                {
                    naveg_remove_control(q);
                }

                naveg_turn_on_pagination_leds();

                //clear actuator queue
                reset_queue();

                ui_comm_webgui_send(buffer, i);

                if (!g_self_test_mode) {
                    ui_comm_webgui_wait_response();
                }

                //now notify host
                char val_buffer[20] = {0};
                sys_comm_set_response_cb(NULL, NULL);

                int_to_str(page, val_buffer, sizeof(val_buffer), 0);

                sys_comm_send(CMD_SYS_PAGE_CHANGE, val_buffer);
            }
        break;

        //pagination
        case 5:;
            char buffer[10];
            uint8_t i, j;
            i = copy_command(buffer, CMD_NEXT_PAGE);

            if (!g_page_mode)
            {
                if (!pressed) return;

                uint8_t pagefound = 0;
                j = page;
                while (!pagefound)
                {
                    j++;
                    if (j >= PAGES_COUNT)
                    {
                        j = 0;
                    }

                    //we went in a loop, only one page
                    if (j == page)
                    {
                        break;
                    }

                    //page found
                    if (page_available[j] == 1)
                    {
                        page = j;
                        pagefound = 1;
                        break;
                    }
                }

                if (!pagefound)
                    return;

                i += int_to_str(page, &buffer[i], sizeof(buffer) - i, 0);

                //clear controls            
                uint8_t q;
                for (q = 0; q < TOTAL_ACTUATORS; q++)
                {
                    naveg_remove_control(q);
                }

                naveg_turn_on_pagination_leds();

                //clear actuator queue
                reset_queue();

                ui_comm_webgui_send(buffer, i);

                if (!g_self_test_mode) {
                    ui_comm_webgui_wait_response();
                }

                //now notify host
                char val_buffer[20] = {0};
                sys_comm_set_response_cb(NULL, NULL);

                int_to_str(page, val_buffer, sizeof(val_buffer), 0);

                sys_comm_send(CMD_SYS_PAGE_CHANGE, val_buffer);
            }
            //other navigation mode, we are a snapshot button now
            else 
            {
                if (snapshot_loaded[foot - FOOTSWITCHES_ACTUATOR_COUNT] > 0)
                {
                    if (pressed == 1)
                    {
                        ledz_t *led = hardware_leds(foot);
                        led->led_state.color = SNAPSHOT_LOAD_COLOR;
                        ledz_set_state(led, LED_ON, LED_UPDATE);
                    }
                    else
                    {
                        if ( snapshot_loaded[foot - FOOTSWITCHES_ACTUATOR_COUNT] == 1)
                        {
                            ledz_t *led = hardware_leds(foot);
                            led->led_state.color = SNAPSHOT_COLOR;
                            ledz_set_state(led, LED_ON, LED_UPDATE);

                            char buffer[10];
                            uint8_t i;

                            i = copy_command(buffer, CMD_DUOX_SNAPSHOT_LOAD);

                            i += int_to_str(foot - FOOTSWITCHES_ACTUATOR_COUNT, &buffer[i], sizeof(buffer) - i, 0);

                            //do not do actuators here
                            reset_queue();

                            ui_comm_webgui_send(buffer, i);
                            if (!g_self_test_mode) {
                                ui_comm_webgui_wait_response();
                            }
                        }
                        else
                        {
                            snapshot_loaded[foot - FOOTSWITCHES_ACTUATOR_COUNT] = 1;
                        }
                    }
                }
            }      
        break;
    }
}

void naveg_draw_foot(control_t *control)
{
    foot_control_add(control);
}

void naveg_reset_page(void)
{
    //reset variable
    page = 0;

    page_available[0] = 1;
    page_available[1] = 0;
    page_available[2] = 0;
    page_available[3] = 0;
    page_available[4] = 0;
    page_available[5] = 0;

    naveg_turn_on_pagination_leds();

    return;
}

void naveg_save_snapshot(uint8_t foot)
{
   // if is in tool or selftest mode return
    if (g_self_test_mode) return;

    //depending on the mode we support this function
    if ((!g_page_mode)&&(foot == 5)) return;
    if ((g_page_mode)&&(foot !=5)) return;

    char buffer[128];
    uint8_t i;

    //if in menu return
    if (display_has_tool_enabled(DISPLAY_LEFT)) return;

    i = copy_command(buffer, CMD_DUOX_SNAPSHOT_SAVE);

    ledz_t *led = hardware_leds(foot);
    led->led_state.color = SNAPSHOT_COLOR;
    ledz_set_state(led, LED_ON, LED_UPDATE);
    ledz_blink(hardware_leds(foot), RED, 85, 85, 3);

    i += int_to_str(foot - FOOTSWITCHES_ACTUATOR_COUNT, &buffer[i], sizeof(buffer) - i, 0);

    ui_comm_webgui_send(buffer, i);

	snapshot_loaded[foot - FOOTSWITCHES_ACTUATOR_COUNT] = 2;
}

void naveg_clear_snapshot(uint8_t foot)
{
    snapshot_loaded[foot - FOOTSWITCHES_ACTUATOR_COUNT] = 0;
    ledz_t *led = hardware_leds(foot);
    led->led_state.color = MAX_COLOR_ID;
    ledz_set_state(led, LED_OFF, LED_UPDATE);
}

void naveg_reload_display(void)
{
    screen_clear(0);
    screen_clear(1);

    //disp 1
    naveg_print_pb_name(0);
    control_t *control = g_encoders[0];
    screen_encoder(0, control);
    draw_all_pots(0);
    draw_all_foots(0);

    //disp 2
    control = g_encoders[1];
    naveg_master_volume(0);
    draw_all_pots(1);
    screen_encoder(1, control);
    draw_all_foots(1);
}

void naveg_toggle_tool(uint8_t tool, uint8_t display)
{
    if (!g_initialized) return;

    //no tools in selftest
    if (g_self_test_mode)
    {
        naveg_enter(display);
        return;
    }

    static uint8_t banks_loaded = 0;
    // clears the display
    screen_clear(display);

    // changes the display to tool mode
    if (!tool_is_on(tool))
    {
        // action to do when the tool is enabled
        switch (tool)
        {
            case DISPLAY_TOOL_NAVIG:
                // initial state to banks/pedalboards navigation
                if (!banks_loaded) request_banks_list(2);
                banks_loaded = 1;
                tool_off(DISPLAY_TOOL_SYSTEM_SUBMENU);
                display = 1;
                g_bp_state = BANKS_LIST;
                break;
            case DISPLAY_TOOL_TUNER:
            	display_disable_all_tools(display);

                ui_comm_webgui_send(CMD_TUNER_ON, strlen(CMD_TUNER_ON));
                break;
            case DISPLAY_TOOL_SYSTEM:
            	screen_clear(1);
                tool_off(DISPLAY_TOOL_MASTER_VOL);
            	tool_on(DISPLAY_TOOL_SYSTEM_SUBMENU, 1);
        }

        // draws the tool
        tool_on(tool, display);
        screen_tool(tool, display);

        if (tool == DISPLAY_TOOL_SYSTEM)
        {
            //already enter banks menu on display 1
            menu_enter(1);
        	g_current_main_menu = g_current_menu;
    		g_current_main_item = g_current_item;
            menu_enter(0);
        }
    }
    // changes the display to control mode
    else
    {
        // action to do when the tool is disabled
        switch (tool)
        {
            case DISPLAY_TOOL_SYSTEM:
                display_disable_all_tools(display);
                display_disable_all_tools(1);
                g_update_cb = NULL;
                g_update_data = NULL;
                banks_loaded = 0;
                // force save gains when leave the menu
                system_save_gains_cb(NULL, MENU_EV_ENTER);
                break;
            case DISPLAY_TOOL_NAVIG:
            	tool_off(DISPLAY_TOOL_NAVIG);
            	tool_on(DISPLAY_TOOL_SYSTEM_SUBMENU, 1);
            	return;
            	break;

            case DISPLAY_TOOL_TUNER:
                ui_comm_webgui_send(CMD_TUNER_OFF, strlen(CMD_TUNER_OFF));
                tool_off(DISPLAY_TOOL_TUNER);
                tool_on(DISPLAY_TOOL_SYSTEM_SUBMENU, 1);
                return;
                break;
        }

        control_t *control = g_encoders[display];

        // draws the control
        screen_encoder(display, control);

        //display pb info
        naveg_print_pb_name(display);

        //draws the pots
        draw_all_pots(display);

        //draws the foots
        draw_all_foots(display);

        if (tool == DISPLAY_TOOL_SYSTEM)
        {
        	screen_clear(1);
        	control = g_encoders[1];
        	display = 1;

            //display master volume
            naveg_master_volume(0);

            //draws the pots
            draw_all_pots(display);

        	// draws the control
        	screen_encoder(display, control);

            //draws the foots
            draw_all_foots(display);
        }
    }
}

uint8_t naveg_is_tool_mode(uint8_t display)
{
    return display_has_tool_enabled(display);
}

void naveg_master_volume(uint8_t set)
{
    if (tool_is_on(DISPLAY_TOOL_SYSTEM) || (g_self_test_mode))
    {
        naveg_enter(set);
        return;
    }

    menu_item_t *item = naveg_get_menu_item_by_ID(PB_GAIN_OUTP);

    if (set == 1)
    {
        if (tool_is_on(DISPLAY_TOOL_MASTER_VOL))
        {
            tool_off (DISPLAY_TOOL_MASTER_VOL);
            system_save_gains_cb(NULL, MENU_EV_ENTER);
            screen_master_vol(item->data.value);
            return;
        }
        else
        {
            tool_on(DISPLAY_TOOL_MASTER_VOL, 1);
        }
    }

    //convert value for screen
    screen_master_vol(item->data.value);

}

void naveg_pages_available(uint8_t page_1, uint8_t page_2, uint8_t page_3, uint8_t page_4, uint8_t page_5, uint8_t page_6)
{
    page_available[0] = page_1;
    page_available[1] = page_2;
    page_available[2] = page_3;
    page_available[3] = page_4;
    page_available[4] = page_5;
    page_available[5] = page_6;

    naveg_turn_on_pagination_leds();
}

void naveg_print_pb_name(uint8_t display)
{
    (void) display;
    screen_top_info(NULL, 0);
}

void naveg_set_master_volume(uint8_t set)
{
    if (!tool_is_on(DISPLAY_TOOL_MASTER_VOL)) return;

    if (set)
        system_master_volume_cb(MENU_EV_DOWN);
    else
        system_master_volume_cb(MENU_EV_UP);

    menu_item_t *item = naveg_get_menu_item_by_ID(PB_GAIN_OUTP);

    screen_master_vol(item->data.value);
}

uint8_t naveg_is_master_vol(void)
{
    return tool_is_on(DISPLAY_TOOL_MASTER_VOL) ? 1 : 0;
}

void naveg_set_banks(bp_list_t *bp_list)
{
    if (!g_initialized) return;
    g_banks = bp_list;
}

bp_list_t *naveg_get_banks(void)
{
    if (!g_initialized) return NULL;

    return g_banks;
}

void naveg_set_pedalboards(bp_list_t *bp_list)
{
    if (!g_initialized) return;

    g_naveg_pedalboards = bp_list;
}

bp_list_t *naveg_get_pedalboards(void)
{
    if (!g_initialized) return NULL;

    return g_naveg_pedalboards;
}

void naveg_enter(uint8_t display)
{
    //if in selftest mode, we just send if we are working or not
    if ((g_self_test_mode) && !g_dialog_active)
    {
        char buffer[30];
        uint8_t i;

        i = copy_command(buffer, CMD_SELFTEST_ENCODER_CLICKED);

        // insert the hw_id on buffer
        i += int_to_str(display, &buffer[i], sizeof(buffer) - i, 0);

        ui_comm_webgui_clear_tx_buffer();

        // send the data to GUI
        ui_comm_webgui_send(buffer, i);

        return;
    }

    if ((!g_initialized)&&(g_update_cb)) return;

    if (display_has_tool_enabled(display))
    {
        if (display == 0)
        {
            //we dont use this knob in dialog mode
            if (g_dialog_active) return;

            if (tool_is_on(DISPLAY_TOOL_TUNER) || g_dialog_active)
            {
                menu_enter(display);
            }
            else if ((g_current_item->desc->id == TEMPO_ID) || (g_current_item->desc->id == BYPASS_ID))
            {
                // calls the action callback
                if ((g_current_item->desc->action_cb)) g_current_item->desc->action_cb(g_current_item, MENU_EV_ENTER);
            }
        }
        else if (display == 1)
        {
            if (tool_is_on(DISPLAY_TOOL_TUNER)) tuner_enter();
            else if (tool_is_on(DISPLAY_TOOL_NAVIG)) bp_enter();
            else if (tool_is_on(DISPLAY_TOOL_SYSTEM_SUBMENU))
            {
            	if ((g_current_menu != g_menu) && (g_current_item->desc->id != ROOT_ID))  menu_enter(display);
            }
        }
    }
}

void naveg_up(uint8_t display)
{
    if (!g_initialized) return;

    //if in selftest mode, we just send if we are working or not
    if ((g_self_test_mode) && !g_dialog_active)
    {
        char buffer[30];
        uint8_t i;

        i = copy_command(buffer, CMD_SELFTEST_ENCODER_LEFT);

        // insert the hw_id on buffer
        i += int_to_str(display, &buffer[i], sizeof(buffer) - i, 0);

        ui_comm_webgui_clear_tx_buffer();

        // send the data to GUI
        ui_comm_webgui_send(buffer, i);

        return;
    }

    if (display_has_tool_enabled(display))
    {
        if (display == 0)
        {
            //we dont use this knob in dialog mode
            if (g_dialog_active) return;

        	if ( (tool_is_on(DISPLAY_TOOL_TUNER)) || (tool_is_on(DISPLAY_TOOL_NAVIG)) )
            {
           			naveg_toggle_tool((tool_is_on(DISPLAY_TOOL_TUNER) ? DISPLAY_TOOL_TUNER : DISPLAY_TOOL_NAVIG), display);
           			tool_on(DISPLAY_TOOL_SYSTEM_SUBMENU, 1);
           			g_current_menu = g_current_main_menu;
            		g_current_item = g_current_main_item;
           			menu_up(display);
           			menu_enter(display);
           	}
            else if (tool_is_on(DISPLAY_TOOL_SYSTEM))
           	{

           		if (((g_current_menu == g_menu) || (g_current_item->desc->id == ROOT_ID)) && (g_dialog_active == false))
           		{
           			g_current_main_menu = g_current_menu;
           			g_current_main_item = g_current_item;
           		}
           		menu_up(display);
           		if (g_dialog_active == false)menu_enter(display);
           	}
        }
        else if (display == 1)
        {
        	if (tool_is_on (DISPLAY_TOOL_MASTER_VOL)) naveg_set_master_volume(1);
            else if (tool_is_on(DISPLAY_TOOL_NAVIG)) bp_up();
            else if (tool_is_on(DISPLAY_TOOL_SYSTEM_SUBMENU))
            {
             	if ((g_current_menu != g_menu) || (g_current_item->desc->id != ROOT_ID)) menu_up(display);
            }
        }
    }
}

void naveg_down(uint8_t display)
{
    if (!g_initialized) return;

    //if in selftest mode, we just send if we are working or not
    if ((g_self_test_mode) && !g_dialog_active)
    {
        char buffer[30];
        uint8_t i;

        i = copy_command(buffer, CMD_SELFTEST_ENCODER_RIGHT);

        // insert the hw_id on buffer
        i += int_to_str(display, &buffer[i], sizeof(buffer) - i, 0);

        ui_comm_webgui_clear_tx_buffer();

        // send the data to GUI
        ui_comm_webgui_send(buffer, i);

        return;
    }

    if (display_has_tool_enabled(display))
    {
        if (display == 0)
        {
            //we dont use this knob in dialog mode
            if (g_dialog_active) return;

        	if ( (tool_is_on(DISPLAY_TOOL_TUNER)) || (tool_is_on(DISPLAY_TOOL_NAVIG)) )
            {
           			naveg_toggle_tool((tool_is_on(DISPLAY_TOOL_TUNER) ? DISPLAY_TOOL_TUNER : DISPLAY_TOOL_NAVIG), display);
           			tool_on(DISPLAY_TOOL_SYSTEM_SUBMENU, 1);
                    g_current_menu = g_current_main_menu;
                    g_current_item = g_current_main_item;
           			menu_down(display);
           			menu_enter(display);
           	}
            else if (tool_is_on(DISPLAY_TOOL_SYSTEM))
           	{
           		if (((g_current_menu == g_menu) || (g_current_item->desc->id == ROOT_ID)) && (g_dialog_active == false))
           		{
           			g_current_main_menu = g_current_menu;
           			g_current_main_item = g_current_item;
           		}
           		menu_down(display);
           		if (g_dialog_active == false) menu_enter(display);
           	}
        }
        else if (display == 1)
        {
        	if (tool_is_on (DISPLAY_TOOL_MASTER_VOL)) naveg_set_master_volume(0);
            else if (tool_is_on(DISPLAY_TOOL_NAVIG)) bp_down();
            else if (tool_is_on(DISPLAY_TOOL_SYSTEM_SUBMENU))
            {
            	if ((g_current_menu != g_menu) || (g_current_item->desc->id != ROOT_ID)) menu_down(display);
            }
        }
    }
}

void naveg_reset_menu(void)
{
    if ((!g_initialized) || (g_self_test_mode)) return;

    g_current_menu = g_menu;
    g_current_item = g_menu->first_child->data;
    g_current_main_menu = g_menu;
    g_current_main_item = g_menu->first_child->data;
    reset_menu_hover(g_menu);
}

int naveg_need_update(void)
{
    return (g_update_cb ? 1: 0);
}

void naveg_update(void)
{
    if (g_update_cb)
    {
        (*g_update_cb)(g_update_data, MENU_EV_ENTER);
        screen_system_menu(g_update_data);
    }
}

void naveg_set_active_pedalboard(uint8_t pedalboard_index)
{
    //0th item is "back to banks list"
    uint32_t index = pedalboard_index + 1;

    g_current_pedalboard = index;

    if (g_current_bank == g_banks->selected) {
        g_naveg_pedalboards->hover = index;
        g_naveg_pedalboards->selected = index;
    }
}

void naveg_set_pb_list_update(void)
{
    g_pedalboards_need_update = true;
}

bool naveg_get_pb_list_update(void)
{
    return g_pedalboards_need_update;
}

void naveg_update_pb_list(void)
{
    if (tool_is_on(DISPLAY_TOOL_NAVIG))
    {
        if (g_bp_state == PEDALBOARD_LIST) {
            request_pedalboards(PAGE_DIR_INIT, atoi(g_banks->uids[g_banks->selected - g_banks->page_min]));

            char *title = g_banks->names[g_banks->selected - g_banks->page_min];
            screen_bp_list(title, g_naveg_pedalboards);
        }
    }

    g_pedalboards_need_update = false;
}

uint8_t naveg_dialog(const char *msg, char *title)
{
    static node_t *dummy_menu = NULL;
    static menu_desc_t desc = {NULL, MENU_CONFIRM2, DIALOG_ID, DIALOG_ID, NULL, 0};

    if (!dummy_menu)
    {
        menu_item_t *item;
        item = (menu_item_t *) MALLOC(sizeof(menu_item_t));
        item->data.hover = 0;
        item->data.selected = 0xFF;
        item->data.list_count = 2;
        item->data.list = NULL;
        item->data.popup_content = msg;
        if (title)
            item->data.popup_header = title;
        else
            item->data.popup_header = "WARNING";
        item->desc = &desc;
        item->name = NULL;
        dummy_menu = node_create(item);
    }

    display_disable_all_tools(DISPLAY_LEFT);
    tool_on(DISPLAY_TOOL_SYSTEM, DISPLAY_LEFT);
    tool_on(DISPLAY_TOOL_SYSTEM_SUBMENU, DISPLAY_RIGHT);

    g_current_menu = dummy_menu;
    g_current_item = dummy_menu->data;

    screen_clear(DISPLAY_LEFT);
    screen_clear(DISPLAY_RIGHT);

    screen_system_menu(g_current_item);

    g_dialog_active = 1;
    //force device being booted, as mod-ui wont notify in selftest mode
    g_device_booted = true;

    if (xSemaphoreTake(g_dialog_sem, portMAX_DELAY) == pdTRUE)
    {
        g_dialog_active = 0;
        display_disable_all_tools(DISPLAY_LEFT);
        display_disable_all_tools(DISPLAY_RIGHT);
        
        g_update_cb = NULL;
        g_update_data = NULL;
        
        naveg_reset_menu();

        screen_clear(DISPLAY_RIGHT);
        return g_current_item->data.hover;
    }
    //we can never get here, portMAX_DELAY means wait indefinatly I'm adding this to remove a compiler warning
    else
    {
        //ERROR
        return -1; 
    }
}

uint8_t naveg_ui_status(void)
{
    return g_ui_connected;
}

void naveg_lock_pots(uint8_t lock)
{
    g_lock_potentiometers = lock;
}

void naveg_set_page_mode(uint8_t mode)
{
    g_page_mode = mode;
}

uint8_t naveg_dialog_status(void)
{
    return g_dialog_active;
}

uint8_t naveg_banks_mode_pb(void)
{
    return g_bp_state;
}

char* naveg_get_current_pb_name(void)
{
    return g_banks->names[g_banks->hover];
}

uint8_t naveg_tap_tempo_status(uint8_t id)
{
	if (g_tap_tempo[id].state == TT_INIT) return 0;
	else return 1;
}

void naveg_settings_refresh(uint8_t display_id)
{
    display_id ? screen_system_menu(g_current_item) : screen_system_menu(g_current_main_item);
}


void naveg_menu_refresh(uint8_t display_id)
{
	node_t *node = display_id ? g_current_menu : g_current_main_menu;

	//updates all items in a menu
	for (node = node->first_child; node; node = node->next)
	{
 		// gets the menu item
        menu_item_t *item = node->data;

        // calls the action callback
        if ((item->desc->action_cb)) item->desc->action_cb(item, MENU_EV_NONE);
	}
    naveg_settings_refresh(display_id);
}

//the menu refresh is to slow for the gains so this one is added that only updates the set value.
void naveg_update_gain(uint8_t display_id, uint8_t update_id, float value, float min, float max)
{
    node_t *node = display_id ? g_current_menu : g_current_main_menu;

    //updates all items in a menu
    for (node = node->first_child; node; node = node->next)
    {
        // gets the menu item
        menu_item_t *item = node->data;

        // updates the value
        if ((item->desc->id == update_id))
        {
            item->data.value = value;
            char str_buf[10];

            if ((update_id == IN2_VOLUME) || (update_id == IN1_VOLUME)) {
                float value_bfr;
                value_bfr = MAP(item->data.value, min, max, -12, 25);
                float_to_str(value_bfr, str_buf, sizeof(str_buf), 2);
            }
            else {
                float_to_str(item->data.value, str_buf, sizeof(str_buf), 2);
            }

            strcpy(item->name, item->desc->name);
            uint8_t q;
            uint8_t value_size = strlen(str_buf);
            uint8_t name_size = strlen(item->name);
            for (q = 0; q < (31 - name_size - value_size - 3); q++)
            {
                strcat(item->name, " ");
            }
            strcat(item->name, str_buf);
            strcat(item->name, " dB");
        }
    }
}

void naveg_menu_item_changed_cb(uint8_t item_ID, uint16_t value)
{
    //set value in system.c
    system_update_menu_value(item_ID, value);

    //are we inside the menu? if so we need to update
    if (tool_is_on(DISPLAY_TOOL_SYSTEM))
    {
        //menu update for left or right? or both? 

        //if left menu item, no need to change right
        if((item_ID == TUNER_ID) || (item_ID == TEMPO_ID))
        {
            naveg_menu_refresh(DISPLAY_LEFT);
            return;
        }
        
        //otherwise update right for sure
        else 
        {
            if (!tool_is_on(DISPLAY_TOOL_TUNER) && !tool_is_on(DISPLAY_TOOL_NAVIG))
            {
                naveg_menu_refresh(DISPLAY_RIGHT);
            }

            //for bypass, left might change as well, we update just in case
            if (((item_ID - BYPASS_ID) < 10) && ((item_ID - BYPASS_ID) > 0) )
            {
               naveg_menu_refresh(DISPLAY_LEFT); 
            }
        } 
    }

    //when we are not in the menu, did we change the master volume link?
        //TODO update the master volume link widget
}

//function used to update the calibration value's and restore the LED states
//function is toggled after the calibration is exited
void naveg_update_calibration(void)
{
    //update calibration value's
    uint8_t i = 0;
    for (i = 0; i < POTS_COUNT; i++)
    {
        //get the calibration for this pot
        g_pot_calibrations[0][i] = calibration_get_min(i);
        g_pot_calibrations[1][i] = calibration_get_max(i);
    }

    //update the led states
    for (i=0; i < LEDS_COUNT; i++)
    {
        //ledz_restore_state(hardware_leds(i), i);
    }
}

void naveg_turn_on_pagination_leds(void)
{
    uint8_t page_color;

    switch (page)
    {
        case 0:
            page_color = PAGES1_COLOR;
        break;
        case 1:
            page_color = PAGES2_COLOR;
        break;
        case 2:
            page_color = PAGES3_COLOR;
        break;
        case 3:
            page_color = PAGES4_COLOR;
        break;
        case 4:
            page_color = PAGES5_COLOR;
        break;
        case 5:
            page_color = PAGES6_COLOR;
        break;
        default:
            page_color = PAGES1_COLOR;
        break;
    }

    if (!g_page_mode)
    {
        ledz_t *led = hardware_leds(5);

        led->led_state.color = page_color;

        ledz_set_state(led, LED_ON, LED_UPDATE);

        led = hardware_leds(4);
        led->led_state.color = SNAPSHOT_COLOR;
        if (snapshot_loaded[0])
            ledz_set_state(led, LED_ON, LED_UPDATE);
        else
            ledz_set_state(led, LED_OFF, LED_UPDATE);

        led = hardware_leds(6);
        led->led_state.color = SNAPSHOT_COLOR;
        if (snapshot_loaded[2])
            ledz_set_state(led, LED_ON, LED_UPDATE);
        else
            ledz_set_state(led, LED_OFF, LED_UPDATE);
    }
    else 
    {
        ledz_t *led_page_1 = hardware_leds(4);
        ledz_t *led_page_2 = hardware_leds(6);

        led_page_1->led_state.color = page_color;
        led_page_2->led_state.color = page_color;

        ledz_set_state(led_page_1, LED_ON, LED_UPDATE);
        ledz_set_state(led_page_2, LED_ON, LED_UPDATE);

        ledz_t *led = hardware_leds(5);
        led->led_state.color = SNAPSHOT_COLOR;
        if (snapshot_loaded[1])
            ledz_set_state(led, LED_ON, LED_UPDATE);
        else
            ledz_set_state(led, LED_OFF, LED_UPDATE);
    }
}

menu_item_t *naveg_get_menu_item_by_ID(uint16_t menu_id)
{
    return get_menu_node_by_ID(menu_id)->data;
}

void naveg_set_reboot_value(uint8_t boot_value)
{
    g_reboot_value = boot_value;
}

void naveg_turn_off_leds(void)
{
    uint8_t i;
    ledz_t *led; 
    for (i = 0; i < LEDS_COUNT; i++)
    {
        led = hardware_leds(i);
        led_state_t led_state = {
            .color = WHITE,
        };
        set_ledz_trigger_by_color_id(led, LED_OFF, led_state);
    }
}