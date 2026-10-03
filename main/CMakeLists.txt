idf_component_register(
    SRCS "main.c" "pud.c" "board_lgfx.cpp" "board_touch.c" "lvgl_demo.cpp"
         "usb/usb.c" "usb/usbd_vendor.c"
         "decoder/decoder.c" "decoder/decoder_qoi.c" "decoder/rgb565_qoi.c"
    INCLUDE_DIRS "include" "usb" "decoder"
    REQUIRES LovyanGFX lvgl CherryUSB esp_timer efuse
)
