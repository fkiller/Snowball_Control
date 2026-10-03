#include QMK_KEYBOARD_H
#include "raw_hid.h"
#include "uart.h"
#include "via.h"

#if 0
// Send information to the keyboard
send_string("test");

// Send information to Interface 1 HID Report Descriptor Vendor-Defined 97
uint8_t data[32] = "test";;
raw_hid_send(data, sizeof(data));

// Send information to the serial port
uart_write

// Send information to the console (QMK Toolbox)
uprintf("KL: kc: 0x%04X, col: %2u, row: %2u, pressed: %u, time: %5u, int: %u, count: %u\n", keycode, record->event.key.col, record->event.key.row, record->event.pressed, record->event.time, record->tap.interrupted, record->tap.count);

// Simulate key press 1
#define MIN_DELAY 10
#define MAX_DELAY 50
uint16_t random_delay = rand() % (MAX_DELAY - MIN_DELAY + 1) + MIN_DELAY;
uint16_t held_keycode_timer = timer_read();
register_code16(KC_C);
while (timer_elapsed(held_keycode_timer) < random_delay){ /* no-op */ }
unregister_code16(KC_C);

// Simulate key press 2
uint8_t layer = get_highest_layer(layer_state);
uint16_t kc = keymap_key_to_keycode(layer, (keypos_t) {.row = 0, .col = 1});
uint16_t held_keycode_timer = timer_read();
register_code16(kc);
while (timer_elapsed(held_keycode_timer) < random_delay){ /* no-op */ }
unregister_code16(kc);
#endif

#define UART_MATRIX_RESPONSE_TIMEOUT 10000

uint16_t encoder_keycodes[NUM_ENCODERS][3] = {
    {KC_NO,KC_NO,KC_NO},
    {KC_NO,KC_NO,KC_NO},
};

void initCustomKeys(void);
void scanCustomKeys(void);

static inline void _delay_us(uint32_t delay) {  
    for (uint32_t i = 0; i < 72 * delay; i++) {  
        __NOP();
    }  
}  

void matrix_init_user(void) {
    uart_init(115200);
    setPinOutput(A8); 

    writePin(A8, 1); 
    _delay_us(3);
    writePin(A8, 0); 
    _delay_us(3);

    writePin(A8, 1); 
    _delay_us(3);
    writePin(A8, 0); 
    _delay_us(3);

    writePin(A8, 1); 
    _delay_us(3);
    writePin(A8, 0); 
    _delay_us(3);

    writePin(A8, 1); 

    initCustomKeys();
}

enum custom_command_id {
    id_custom_report_key_state = id_dynamic_keymap_set_encoder + 0x01,
    id_custom_request_upload_key,
    id_custom_request_upload_mouse,
    id_custom_request_bootloader_jump,
    id_custom_request_upload_string,
    id_custom_get_eeprom_type,
};

enum qmk_mouse_event_e{
    qmk_mouse_event_down,
    qmk_mouse_event_up,
    qmk_mouse_event_click,
    qmk_mouse_event_doubleClick,
    qmk_mouse_event_none,
};

typedef struct {
    report_mouse_t report;
    enum qmk_mouse_event_e event;
} PACKED c_report_mouse_t;
void mouse_control(c_report_mouse_t *c_report_mouse);

void packFrame(uint8_t* frame ,uint8_t *data, uint8_t dataLen);

void send_string_safely(const char *str) {
    bool caps = host_keyboard_led_state().caps_lock;
    if (caps) {
        tap_code(KC_CAPS);
        wait_ms(50);
    }
    send_string(str);
    if (caps) {
        tap_code(KC_CAPS);
        wait_ms(50);
    }
}
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if(record->event.key.row > 100 || record->event.key.col > 100){// Encoder escalation is rejected
        return true;
    }

    //if(keycode == KC_NO){
        uint8_t data_[] = {id_custom_report_key_state , record->event.pressed , record->event.key.row , record->event.key.col,(keycode >> 8) & 0x00FF,keycode & 0x00FF};
        uint8_t frame[2 + 1 + 2 + 255 + 2];
        packFrame(frame , data_, sizeof(data_) / sizeof(data_[0]));
        uart_transmit(frame, 2 + 1 + 2 + sizeof(data_) / sizeof(data_[0]) + 2);
    //}
    return true;
}

bool encoder_update_kb(uint8_t index, bool clockwise) {
    if(index >= NUM_ENCODERS){
        return true;
    }
    if(encoder_keycodes[index][clockwise ? 2: 1] != KC_NO){
        tap_code16(encoder_keycodes[index][clockwise ? 2: 1]);
    }
    else{
        int row = 100 + (index*3) + (clockwise ? 2: 1);
        int col = row;

        uint8_t data_[] = {id_custom_report_key_state,1,row,col,0,0};
        uint8_t frame[2 + 1 + 2 + 255 + 2];
        packFrame(frame , data_, sizeof(data_) / sizeof(data_[0]));
        uart_transmit(frame, 2 + 1 + 2 + sizeof(data_) / sizeof(data_[0]) + 2);
    }
    return true;
}

void parseByte(uint8_t byte);
void processData(void);
void matrix_scan_user(void) {
    uint32_t timeout = 0;

    scanCustomKeys();

    while(1){
        if (uart_available()) {
            parseByte(uart_read());
            timeout = 0;
        }
        else{
            timeout++;
        }
        if (timeout > UART_MATRIX_RESPONSE_TIMEOUT) {
            break;
        }
    }
}

typedef enum {
    STATE_HEADER1,
    STATE_HEADER2,
    STATE_CHECKSUM,
    STATE_DATA_LEN,
    STATE_DATA_LEN_CHECK,
    STATE_DATA,
    STATE_TAIL1,
    STATE_TAIL2
} ParseState;

ParseState currentState = STATE_HEADER1;
uint8_t checksum = 0;
uint8_t dataLen = 0;
uint8_t data[0xff];
uint8_t dataIndex = 0;
void packFrame(uint8_t* frame ,uint8_t *data, uint8_t dataLen) {
    frame[0] = 0xAA;// header
    frame[1] = 0x55;// header
    frame[2] = 0;// checksum
    frame[3] = dataLen;// dataLen
    frame[4] = 0xFF - dataLen;// dataLen check
    memcpy(&frame[5],data,dataLen);// data
    frame[5 + dataLen + 0] = 0xF5;// tail
    frame[5 + dataLen + 1] = 0x5F;// tail
    for (int i = 0; i < dataLen; i++) {
        frame[2] += data[i];// checksum
    }
}

int scanClearState(unsigned char receivedByte);

void parseByte(uint8_t byte) {
    if(scanClearState(byte) == 0 ){
        currentState = STATE_HEADER1;
        uprintf("c\n");
    }

    switch (currentState) {
    case STATE_HEADER1:
        if (byte == 0xAA) {
            // Reset variables
            checksum = 0;
            dataLen = 0;
            dataIndex = 0;

            currentState = STATE_HEADER2;
        }
        else{
            currentState = STATE_HEADER1;
            uprintf("e 1\n");
        }
        break;

    case STATE_HEADER2:
        if (byte == 0x55) {
            currentState = STATE_CHECKSUM;
        }
        else{
            currentState = STATE_HEADER1;
            uprintf("e 2\n");
        }

        break;

    case STATE_CHECKSUM:
        checksum = byte;
        currentState = STATE_DATA_LEN;
        break;

    case STATE_DATA_LEN:
        dataLen = byte;
        currentState = STATE_DATA_LEN_CHECK;
        break;

    case STATE_DATA_LEN_CHECK:
        if(0xFF - dataLen == byte){
            currentState = STATE_DATA;
        }
        else{
            currentState = STATE_HEADER1;
            uprintf("e 3 0x%x - 0x%x \n", dataLen,byte);
        }
        break;

    case STATE_DATA:
        data[dataIndex++] = byte;
        if (dataIndex == dataLen) {
            currentState = STATE_TAIL1;
        }
        break;

    case STATE_TAIL1:
        if (byte == 0xF5) {
            currentState = STATE_TAIL2;
        } else {
            currentState = STATE_HEADER1;
            uprintf("e 4\n");
        }
        break;

    case STATE_TAIL2:
        if (byte == 0x5F) {
            // Validate checksum
            uint8_t calculatedChecksum = 0;
            for (int i = 0; i < dataLen; i++) {
                calculatedChecksum += data[i];
            }
            if (calculatedChecksum == checksum) {
                processData();
                currentState = STATE_HEADER1;
                uprintf("i f\n");
            } else {
                currentState = STATE_HEADER1;
                uprintf("e 5\n");
            }
        } else {
            currentState = STATE_HEADER1;
            uprintf("e 6\n");
        }
        break;

    default:
        break;
    }
}
//copy from qmk_firmware\quantum\via.c
extern bool via_command_kb(uint8_t *data, uint8_t length) ;
extern void via_custom_value_command(uint8_t *data, uint8_t length);
void processData(void)
{
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);
    uint8_t length = dataLen;
    // If via_command_kb() returns true, the command was fully
    // handled, including calling raw_hid_send()
    if (via_command_kb(data, length)) {
        return;
    }

    switch (*command_id) {
        case id_get_protocol_version: {
            command_data[0] = VIA_PROTOCOL_VERSION >> 8;
            command_data[1] = VIA_PROTOCOL_VERSION & 0xFF;
            break;
        }
        case id_get_keyboard_value: {
            switch (command_data[0]) {
                case id_uptime: {
                    uint32_t value  = timer_read32();
                    command_data[1] = (value >> 24) & 0xFF;
                    command_data[2] = (value >> 16) & 0xFF;
                    command_data[3] = (value >> 8) & 0xFF;
                    command_data[4] = value & 0xFF;
                    break;
                }
                case id_layout_options: {
                    uint32_t value  = via_get_layout_options();
                    command_data[1] = (value >> 24) & 0xFF;
                    command_data[2] = (value >> 16) & 0xFF;
                    command_data[3] = (value >> 8) & 0xFF;
                    command_data[4] = value & 0xFF;
                    break;
                }
                case id_switch_matrix_state: {
                    uint8_t offset = command_data[1];
                    uint8_t rows   = 28 / ((MATRIX_COLS + 7) / 8);
                    uint8_t i      = 2;
                    for (uint8_t row = 0; row < rows && row + offset < MATRIX_ROWS; row++) {
                        matrix_row_t value = matrix_get_row(row + offset);
#if (MATRIX_COLS > 24)
                        command_data[i++] = (value >> 24) & 0xFF;
#endif
#if (MATRIX_COLS > 16)
                        command_data[i++] = (value >> 16) & 0xFF;
#endif
#if (MATRIX_COLS > 8)
                        command_data[i++] = (value >> 8) & 0xFF;
#endif
                        command_data[i++] = value & 0xFF;
                    }
                    break;
                }
                case id_firmware_version: {
                    uint32_t value  = VIA_FIRMWARE_VERSION;
                    command_data[1] = (value >> 24) & 0xFF;
                    command_data[2] = (value >> 16) & 0xFF;
                    command_data[3] = (value >> 8) & 0xFF;
                    command_data[4] = value & 0xFF;
                    break;
                }
                default: {
                    // The value ID is not known
                    // Return the unhandled state
                    *command_id = id_unhandled;
                    break;
                }
            }
            break;
        }
        case id_set_keyboard_value: {
            switch (command_data[0]) {
                case id_layout_options: {
                    uint32_t value = ((uint32_t)command_data[1] << 24) | ((uint32_t)command_data[2] << 16) | ((uint32_t)command_data[3] << 8) | (uint32_t)command_data[4];
                    via_set_layout_options(value);
                    break;
                }
                case id_device_indication: {
                    uint8_t value = command_data[1];
                    via_set_device_indication(value);
                    break;
                }
                default: {
                    // The value ID is not known
                    // Return the unhandled state
                    *command_id = id_unhandled;
                    break;
                }
            }
            break;
        }
        case id_dynamic_keymap_get_keycode: {
            uint16_t keycode = dynamic_keymap_get_keycode(command_data[0], command_data[1], command_data[2]);
            command_data[3]  = keycode >> 8;
            command_data[4]  = keycode & 0xFF;
            break;
        }
        case id_dynamic_keymap_set_keycode: {
            if(command_data[1] >= 100){
                if(command_data[1] < (100 + (sizeof(encoder_keycodes) / sizeof(uint16_t) ))) {
                    int index = (command_data[1] - 100) / 3;
                    int function = (command_data[1] - 100) % 3;
                    encoder_keycodes[index][function] = (command_data[3] << 8) | command_data[4];
                }
            }
            else{
                dynamic_keymap_set_keycode(command_data[0], command_data[1], command_data[2], (command_data[3] << 8) | command_data[4]);
            }
            break;
        }
        case id_dynamic_keymap_reset: {
            dynamic_keymap_reset();
            break;
        }
        case id_custom_set_value:
        case id_custom_get_value:
        case id_custom_save: {
            via_custom_value_command(data, length);
            break;
        }
#ifdef VIA_EEPROM_ALLOW_RESET
        case id_eeprom_reset: {
            via_eeprom_set_valid(false);
            eeconfig_init_via();
            break;
        }
#endif
        case id_dynamic_keymap_macro_get_count: {
            command_data[0] = dynamic_keymap_macro_get_count();
            break;
        }
        case id_dynamic_keymap_macro_get_buffer_size: {
            uint16_t size   = dynamic_keymap_macro_get_buffer_size();
            command_data[0] = size >> 8;
            command_data[1] = size & 0xFF;
            break;
        }
        case id_dynamic_keymap_macro_get_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_macro_get_buffer(offset, size, &command_data[3]);
            break;
        }
        case id_dynamic_keymap_macro_set_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_macro_set_buffer(offset, size, &command_data[3]);
            break;
        }
        case id_dynamic_keymap_macro_reset: {
            dynamic_keymap_macro_reset();
            break;
        }
        case id_dynamic_keymap_get_layer_count: {
            command_data[0] = dynamic_keymap_get_layer_count();
            break;
        }
        case id_dynamic_keymap_get_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_get_buffer(offset, size, &command_data[3]);
            break;
        }
        case id_dynamic_keymap_set_buffer: {
            uint16_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2]; // size <= 28
            dynamic_keymap_set_buffer(offset, size, &command_data[3]);
            break;
        }
#ifdef ENCODER_MAP_ENABLE
        case id_dynamic_keymap_get_encoder: {
            uint16_t keycode = dynamic_keymap_get_encoder(command_data[0], command_data[1], command_data[2] != 0);
            command_data[3]  = keycode >> 8;
            command_data[4]  = keycode & 0xFF;
            break;
        }
        case id_dynamic_keymap_set_encoder: {
            dynamic_keymap_set_encoder(command_data[0], command_data[1], command_data[2] != 0, (command_data[3] << 8) | command_data[4]);
            break;
        }
#endif
        case id_custom_request_upload_key:
        switch (command_data[0])
        {
        case 0:{
            uint16_t keycode = (command_data[1] << 8) | command_data[2];
            if (keycode >= QK_MACRO && keycode <= QK_MACRO_MAX) {
                uint8_t id = keycode - QK_MACRO;
                dynamic_keymap_macro_send(id);
            }
            else{
                tap_code16(keycode);
            }
            break;
        }
        case 1:{
            uint16_t keycode = (command_data[1] << 8) | command_data[2];
            register_code16(keycode);
            break;
        }
        case 2:{
            uint16_t keycode = (command_data[1] << 8) | command_data[2];
            unregister_code16(keycode);
            break;
        }
        default:
            break;
        }
        break;
        case id_custom_request_upload_mouse:{
            c_report_mouse_t report_mouse;
            memcpy(&report_mouse,&command_data[0],sizeof(c_report_mouse_t));
            report_mouse.report.buttons = MOUSE_BTN_MASK((int)report_mouse.report.buttons);
            mouse_control(&report_mouse);
            break;
        }
        case id_custom_request_bootloader_jump:{
            bootloader_jump();
            break;
        }
        case id_custom_request_upload_string:{
            send_string_safely((const char *)&command_data[0]);
            break;
        }
        case id_custom_get_eeprom_type:{
#ifdef EEPROM_TRANSIENT
            command_data[0] = 1;
#else
            command_data[0] = 0;
#endif
            break;
        }
        default: {
            // The command ID is not known
            // Return the unhandled state
            *command_id = id_unhandled;
            break;
        }
    }
    uint8_t frame[2 + 1 + 2 + dataLen + 2];
    packFrame(frame , data, length);
    uart_transmit(frame, 2 + 1 + 2 + length + 2);
}


// Define states
typedef enum {
    STATE_INIT, // Initial state
    STATE_02,   // Received 0x02
    STATE_AA,   // Received 0xAA
    STATE_E2,   // Received 0xE2
    STATE_78    // Received 0x78
} State;
State currentClearState = STATE_INIT; // Initial state

int scanClearState(unsigned char receivedByte) {
    // State machine logic
    switch (currentClearState) {
        case STATE_INIT:
            if (receivedByte == 0x02) {
                currentClearState = STATE_02;
            }
            break;

        case STATE_02:
            if (receivedByte == 0xAA) {
                currentClearState = STATE_AA;
            } else {
                currentClearState = STATE_INIT; // Reset
            }
            break;

        case STATE_AA:
            if (receivedByte == 0xE2) {
                currentClearState = STATE_E2;
            } else {
                currentClearState = STATE_INIT; // Reset
            }
            break;

        case STATE_E2:
            if (receivedByte == 0x78) {
                currentClearState = STATE_INIT; // Reset state
                return 0;
            } else {
                currentClearState = STATE_INIT; // Reset
            }
            break;

        default:
            currentClearState = STATE_INIT; // Ensure the state machine resets on error
            break;
    }

    return -1;
}

typedef enum {  
    KEY_STATE_INITIAL,   // Initial state
    KEY_STATE_PRESSED,   // Pressed state
    KEY_STATE_HELD,      // Held state
    KEY_STATE_RELEASED   // Released state
} KeyState;  

typedef struct _Custom_Key{  
    pin_t pin; // Pin connected to the key
    KeyState state;    // Current key state
} Custom_Key;  

void updateKeyState(Custom_Key* _key) {  
    bool keyDown = readPin(_key->pin) == 0 ? true : false;  

    switch (_key->state) {  
        case KEY_STATE_INITIAL:  
            if (keyDown) {  
                _key->state = KEY_STATE_PRESSED;
            }  
            break;
        case KEY_STATE_PRESSED:  
            if (keyDown) {  
                _key->state = KEY_STATE_HELD;  
            } else {  
                _key->state = KEY_STATE_INITIAL;
            }  
            break;  

        case KEY_STATE_HELD:
            if(!keyDown){
                _key->state = KEY_STATE_RELEASED; 
            }
            break;  

        case KEY_STATE_RELEASED:  
            if (!keyDown) {  
                _key->state = KEY_STATE_INITIAL; 
            }
            break;  
    }  
}

#define CUSTOM_KEY_NUMBER NUM_ENCODERS

Custom_Key keys[CUSTOM_KEY_NUMBER] = {
    {B5,KEY_STATE_INITIAL},
    {B4,KEY_STATE_INITIAL},
};
bool keys_press_old[CUSTOM_KEY_NUMBER];
bool keys_press_cur[CUSTOM_KEY_NUMBER];


void initCustomKeys(void)
{
    for (int i = 0; i < CUSTOM_KEY_NUMBER; i++) {  
        setPinInputHigh(keys[i].pin);
        keys_press_old[i] = false;
        keys_press_cur[i] = false;
    } 
}

void scanCustomKeys(void)
{
    for (int i = 0; i < CUSTOM_KEY_NUMBER; i++) {  
        updateKeyState(&keys[i]);  
    }
    for (int i = 0; i < CUSTOM_KEY_NUMBER; i++) {  
        if(keys[i].state == KEY_STATE_PRESSED || keys[i].state == KEY_STATE_HELD){
            keys_press_cur[i] = true;
        }
        else{
            keys_press_cur[i] = false;
        }
    }
    for (int i = 0; i < CUSTOM_KEY_NUMBER; i++) {  
        if(keys_press_old[i] != keys_press_cur[i]){
            keys_press_old[i] = keys_press_cur[i];
            uint16_t keycode = encoder_keycodes[i][0];
            if(keycode != KC_NO) {
                if(keys_press_cur[i]){
                    register_code16(keycode);
                }
                else{
                    unregister_code16(keycode);
                }
            }
            else {
                int row = 100 + (i*3);
                int col = row;

                uint8_t data_[] = {id_custom_report_key_state,keys_press_cur[i],row,col,0,0};
                uint8_t frame[2 + 1 + 2 + 255 + 2];
                packFrame(frame , data_, sizeof(data_) / sizeof(data_[0]));
                uart_transmit(frame, 2 + 1 + 2 + sizeof(data_) / sizeof(data_[0]) + 2);
            }
        }
    }
}

void c_delay(int MIN_DELAY,int MAX_DELAY)
{
    uint16_t random_delay = rand() % (MAX_DELAY - MIN_DELAY + 1) + MIN_DELAY;
    uint16_t held_keycode_timer = timer_read();
    while (timer_elapsed(held_keycode_timer) < random_delay){ /* no-op */ }
}

void mouse_control(c_report_mouse_t *c_report_mouse) {
    if (!c_report_mouse) return;

    switch (c_report_mouse->event) {
        case qmk_mouse_event_none:
            // None
            c_report_mouse->report.buttons = 0;
            pointing_device_set_report(c_report_mouse->report);
            pointing_device_send();
            break;
        case qmk_mouse_event_down:
            // Press mouse button
            c_report_mouse->report.buttons |= c_report_mouse->report.buttons;
            pointing_device_set_report(c_report_mouse->report);
            pointing_device_send();
            break;

        case qmk_mouse_event_up:
            // Release mouse button
            c_report_mouse->report.buttons &= ~c_report_mouse->report.buttons;
            pointing_device_set_report(c_report_mouse->report);
            pointing_device_send();
            break;

        case qmk_mouse_event_click:
            // Single click: press -> release
            c_report_mouse->report.buttons |= c_report_mouse->report.buttons;
            pointing_device_set_report(c_report_mouse->report);
            pointing_device_send();

            c_report_mouse->report.x = 0;
            c_report_mouse->report.y = 0;
            c_report_mouse->report.h = 0;
            c_report_mouse->report.v = 0;
            c_report_mouse->report.buttons &= ~c_report_mouse->report.buttons;
            pointing_device_set_report(c_report_mouse->report);
            pointing_device_send();
            break;

        case qmk_mouse_event_doubleClick:
            // Double click: two single clicks
            for (uint8_t i = 0; i < 2; i++) {
                report_mouse_t report;
                memcpy(&report,&(c_report_mouse->report),sizeof(report_mouse_t));
                report.buttons = 0;
                report.buttons |= c_report_mouse->report.buttons;
                pointing_device_set_report(report);
                pointing_device_send();
                c_delay(20,50);

                report.x = 0;
                report.y = 0;
                report.h = 0;
                report.v = 0;
                report.buttons = 0;
                report.buttons &= ~c_report_mouse->report.buttons;
                pointing_device_set_report(report);
                pointing_device_send();
                c_delay(20,50);
                if(i == 0){
                    c_report_mouse->report.x = 0;
                    c_report_mouse->report.y = 0;
                    c_report_mouse->report.h = 0;
                    c_report_mouse->report.v = 0;
                }
            }
            break;
    }
}
