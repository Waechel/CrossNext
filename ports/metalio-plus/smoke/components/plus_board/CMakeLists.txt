# Metalio E-Ink4-Plus board layer (display: EPDiy + TPS65185 + TCA9555; FT6336U touch).
# Sources copied from RickyOS ports/metalio-e-ink4-plus components/metalio_board (MIT,
# see LICENSE.vendor / NOTICE.md). Stage 1 builds it without the Arduino core.
file(GLOB BUS_SRCS bus/*.c)
file(GLOB DISPLAY_SRCS display/*.c)
list(FILTER DISPLAY_SRCS EXCLUDE REGEX "/font.c$")
file(GLOB OUTPUT_SRCS display/output_lcd/*.c display/output_common/*.c)
idf_component_register(
  SRCS ${BUS_SRCS} ${DISPLAY_SRCS} ${OUTPUT_SRCS}
       display/waveforms/my_waveform.c display/waveforms/my_waveform_trim.c
       smoke_stubs.c plus_bench.c
  INCLUDE_DIRS . bus display display/output_lcd display/output_common display/waveforms
  REQUIRES driver esp_driver_gpio esp_driver_i2c esp_driver_rmt esp_driver_dma
           esp_lcd esp_hal_lcd esp_hal_rmt esp_hal_i2s esp_adc
           esp_timer esp_psram esp_mm esp_hw_support
)
# The accepted RickyOS display profile (scripts/build_rickyos_plus.sh defaults).
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  RICKYOS_PLUS_UI_WHITE_MAINTENANCE=1
  RICKYOS_PLUS_READER_BLACK_RESTORE_PHASES=4
  RICKYOS_PLUS_READER_RETAINED_BLACK_PULSE=17
  RICKYOS_PLUS_READER_QUALITY=1
  RICKYOS_PLUS_READER_EARLY_ERASE=1
  RICKYOS_PLUS_READER_FINE_RIPPLE=1
  RICKYOS_PLUS_READER_MOTION_PERIOD_US=0
  RICKYOS_PLUS_READER_RIPPLE_SPREAD=7
  RICKYOS_PLUS_READER_SWEEP_SPREAD=3
  RICKYOS_PLUS_READER_ERASE_OVERLAP_PHASES=0
  RICKYOS_PLUS_READER_RIPPLE_OVERLAP_PHASES=12
  RICKYOS_PLUS_PHASE_LOOKAHEAD=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
