add_executable(missed_interrupt
        missed_interrupt.cc
        )

add_compile_definitions(
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=1000
)

# pull in common dependencies
target_link_libraries(missed_interrupt pico_stdlib hardware_dma hardware_irq)

pico_enable_stdio_usb(missed_interrupt 0)
pico_enable_stdio_uart(missed_interrupt 0)


# create map/bin/hex file etc.
pico_add_extra_outputs(missed_interrupt)
