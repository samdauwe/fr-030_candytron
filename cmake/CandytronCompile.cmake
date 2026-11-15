# Compile settings shared by every target of the project.
#
#   candytron_configure_target(<target>)
#
# Applies the warning set and, with CANDYTRON_SANITIZE, AddressSanitizer + UndefinedBehaviorSanitizer. The numeric requirements
# (no floating-point contraction, no fast-math) travel with candytron::headers instead, so that consumers inherit them.

function(candytron_configure_target target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
  endif()
  if(CANDYTRON_SANITIZE AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
