/* HID boot-keyboard usage -> ASCII (see hid_usage.h). */
#include "hid_usage.h"

uint16_t hid_usage_to_keycode(uint8_t usage) {
    return usage;
}

char hid_usage_to_char(uint8_t usage, int shift) {
    /* Letters: usage 0x04..0x1D -> 'a'..'z'. */
    if (usage >= 0x04 && usage <= 0x1D) {
        char base = (char)('a' + (usage - 0x04));
        if (shift) {
            base = (char)('A' + (usage - 0x04));
        }
        return base;
    }
    /* Number row: usage 0x1E..0x27 -> '1'..'9','0'. */
    if (usage >= 0x1E && usage <= 0x27) {
        static const char nums[10]   = {'1','2','3','4','5','6','7','8','9','0'};
        static const char shifted[10] = {'!','@','#','$','%','^','&','*','(',')'};
        int i = usage - 0x1E;
        return shift ? shifted[i] : nums[i];
    }
    switch (usage) {
    case 0x28: return '\n';   /* Enter      */
    case 0x29: return 0;      /* Escape     */
    case 0x2A: return '\b';   /* Backspace  */
    case 0x2B: return '\t';   /* Tab        */
    case 0x2C: return ' ';    /* Space      */
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default:   return 0;
    }
}
