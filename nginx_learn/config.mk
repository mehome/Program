#定义项目编译的根目录,通过export把某个变量声明为全局的[其他文件中可以用]，这里获取当前这个文件所在的路径作为根目录；
export BUILD_ROOT = $(shell pwd)

#定义我们要编译的目录
BUILD_DIR = $(BUILD_ROOT)/signal/ \
			$(BUILD_ROOT)/proc/ \
			$(BUILD_ROOT)/net/ \
			$(BUILD_ROOT)/misc/ \
			$(BUILD_ROOT)/business/ \
			$(BUILD_ROOT)/app/

#编译时是否生成调试信息。GNU调试器可以利用该信息
export DEBUG = true