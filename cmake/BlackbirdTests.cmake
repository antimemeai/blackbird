# Test registration for every Blackbird CTest case.
#
# Each case runs under case_launcher (tests/case_launcher.cpp), which ends the
# case's whole process tree, including daemons reparented to init, when the
# case times out, is interrupted, loses its runner or exits leaving processes
# behind. At TIMEOUT CTest sends SIGTERM to the launcher instead of killing it;
# the case is still reported as a timeout at its unchanged deadline, and the
# grace period below is only the launcher's cleanup time before CTest's own
# forced kill. The launcher gives the case 4 s and normally needs milliseconds
# more; the grace is CTest's maximum because CTest's fallback kill runs `ps`,
# which was measured blocking for minutes on this host under load.
if(NOT DEFINED BLACKBIRD_CASE_LAUNCHER)
  set(BLACKBIRD_CASE_LAUNCHER case_launcher) # the target in the main project
endif()

function(blackbird_add_test)
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME" "COMMAND")
  if(NOT arg_NAME OR NOT arg_COMMAND OR arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "blackbird_add_test(NAME name COMMAND program args...)")
  endif()
  # add_test resolves only its first word as a target; that is now the launcher.
  list(POP_FRONT arg_COMMAND program)
  if(TARGET "${program}")
    set(program "$<TARGET_FILE:${program}>")
  endif()
  add_test(NAME ${arg_NAME}
    COMMAND ${BLACKBIRD_CASE_LAUNCHER} -- ${program} ${arg_COMMAND})
  set_tests_properties(${arg_NAME} PROPERTIES
    TIMEOUT_SIGNAL_NAME SIGTERM TIMEOUT_SIGNAL_GRACE_PERIOD 60)
  set_property(GLOBAL APPEND PROPERTY BLACKBIRD_REGISTERED_TESTS ${arg_NAME})
endfunction()

# Refuse a case registered with plain add_test: it would run without the launcher.
function(blackbird_require_registered_tests)
  get_property(all DIRECTORY PROPERTY TESTS)
  get_property(registered GLOBAL PROPERTY BLACKBIRD_REGISTERED_TESTS)
  if(registered)
    list(REMOVE_ITEM all ${registered})
  endif()
  if(all)
    message(FATAL_ERROR "tests registered without blackbird_add_test: ${all}")
  endif()
endfunction()
