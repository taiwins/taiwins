if get_option('rendering-debug') == 'damage'
  options_data.set10('_TW_DEBUG_DAMAGE', true)
elif get_option('rendering-debug') == 'clip'
  options_data.set10('-D_TW_DEBUG_CLIP', true)
endif

taiwins_lib_src += files(
  'render_context.c',
  'render_output.c',
  'render_pipeline.c',
  'egl_render_context.c',
  'egl_texture.c',
  'egl_shaders.c',
  'egl_simple_pipeline.c',
)
