#function to parse libweston function
function(ParseWestonVersion VER LIBWESTON_DIR)
  execute_process(COMMAND sh "-c" "grep libweston_major meson.build | head -1 | cut -d' ' -f3"
    WORKING_DIRECTORY ${LIBWESTON_DIR}
    OUTPUT_VARIABLE OUT_VAR
    ERROR_VARIABLE ERROR_VAR
    RESULT_VARIABLE CMD_RET
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    )
  # message("libweston dir is ${LIBWESTON_DIR}, output variable is ${OUT_VAR}")
  # message("error variable is ${ERROR_VAR}")
  # message("command returns ${CMD_RET}")
  set(${VER} ${OUT_VAR} PARENT_SCOPE)
endfunction()
