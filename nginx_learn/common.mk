ifeq ($(DEBUG),true)
CC = g++ -g
VERSION = debug
else
CC = g++
VERSION = release
endif

CXXFLAGS = -lpthread -std=c++11
SRCS = $(wildcard *.cpp)
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)
BIN := $(addprefix $(BUILD_ROOT)/,$(BIN))

LINK_OBJ_DIR = $(BUILD_ROOT)/app/link_obj
DEP_DIR = $(BUILD_ROOT)/app/dep

$(shell mkdir -p $(LINK_OBJ_DIR))
$(shell mkdir -p $(DEP_DIR))

OBJS := $(addprefix $(LINK_OBJ_DIR)/,$(OBJS))
DEPS := $(addprefix $(DEP_DIR)/,$(DEPS))

LINK_OBJ = $(wildcard $(LINK_OBJ_DIR)/*.o)
LINK_OBJ += $(OBJS)

all:$(DEPS) $(OBJS) $(BIN)

ifneq ("$(wildcard $(DEPS))","")
include $(DEPS)  
endif

$(BIN):$(LINK_OBJ)
	@echo "------------------------build $(VERSION) mode--------------------------------!!!"
# $@ 目标文件的完整名称 
# $^ 所有不重复的依赖，以空格隔开
	$(CC) -o $@ $^ $(CXXFLAGS)

$(LINK_OBJ_DIR)/%.o:%.cpp
	$(CC) -I $(BUILD_ROOT) -o $@ -c $(filter %.cpp,$^)

$(DEP_DIR)/%.d:%.cpp
	$(CC) -I $(BUILD_ROOT) -MM $^ >> $@
