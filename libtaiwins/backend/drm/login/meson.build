logind_version = '>=237'
logind_found = false
dep_login = [dependency('libudev')]

# we will try to find logind
dep_logind = dependency('libsystemd', required: false, version: logind_version)
if dep_logind.found()
  options_data.set10('_TW_HAS_SYSTEMD', true)
  logind_found = true
else
  dep_logind = dependency('libelogind', required: false, version: logind_version)
  if dep_logind.found()
    options_data.set10('_TW_HAS_ELOGIND', true)
    logind_found = true
  endif
endif

if logind_found
  taiwins_lib_src += files('logind.c')
  dep_login += [
    dep_logind,
    dep_tdbus]
endif

taiwins_lib_src += files('login.c', 'direct.c')
taiwins_lib_dep += dep_login
