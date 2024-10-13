project('HailoRT', 'cpp', version : '0.1', default_options : ['warning_level=3', 'cpp_std=c++20'])

# 追加のインクルード
Inc = [
]

# 追加のライブラリ
Compiler = meson.get_compiler('cpp')
Lib = [
	Compiler.find_library('hailort', dirs : '/usr/lib'),
	dependency('opencv4')
]

executable('HailoRT', 'Main.cpp', install : true, include_directories : Inc, dependencies : Lib)
