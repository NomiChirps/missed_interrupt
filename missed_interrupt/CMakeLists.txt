add_executable(missed_interrupt
        missed_interrupt.c
        )

# pull in common dependencies
target_link_libraries(missed_interrupt pico_stdlib)

# create map/bin/hex file etc.
pico_add_extra_outputs(missed_interrupt)
