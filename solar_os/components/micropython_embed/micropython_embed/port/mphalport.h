// Define so there's no dependency on extmod/virtpin.h
#define mp_hal_pin_obj_t

static inline void mp_hal_set_interrupt_char(int c) {
    (void)c;
}
