CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lncurses -lm

TARGET  = cmd_tracker
SRCS    = src/main.c src/db.c src/dashboard.c
OBJS    = $(SRCS:.c=.o)

PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin
SHAREDIR= $(PREFIX)/share/cmd_tracker

.PHONY: all clean install uninstall demo

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(BINDIR)/$(TARGET)
	install -Dm644 shell_hook.bash $(SHAREDIR)/shell_hook.bash
	install -Dm644 shell_hook.zsh  $(SHAREDIR)/shell_hook.zsh
	@echo ""
	@echo "インストール完了!"
	@echo "Bash: echo 'source $(SHAREDIR)/shell_hook.bash' >> ~/.bashrc"
	@echo "Zsh:  echo 'source $(SHAREDIR)/shell_hook.zsh'  >> ~/.zshrc"

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -rf $(SHAREDIR)

# デモ用: サンプルデータを生成してダッシュボードを起動
demo: $(TARGET)
	@echo "サンプルデータを生成中..."
	@./$(TARGET) record git    3.2
	@./$(TARGET) record git    1.8
	@./$(TARGET) record git    5.1
	@./$(TARGET) record ls     0.05
	@./$(TARGET) record ls     0.03
	@./$(TARGET) record ls     0.04
	@./$(TARGET) record ls     0.02
	@./$(TARGET) record vim    45.3
	@./$(TARGET) record vim    120.0
	@./$(TARGET) record make   8.7
	@./$(TARGET) record make   12.3
	@./$(TARGET) record grep   0.1
	@./$(TARGET) record grep   0.2
	@./$(TARGET) record grep   0.15
	@./$(TARGET) record docker 25.4
	@./$(TARGET) record docker 18.2
	@./$(TARGET) record ssh    3.0
	@./$(TARGET) record cat    0.1
	@./$(TARGET) record curl   2.3
	@./$(TARGET) record python 30.0
	@./$(TARGET) record python 45.0
	@./$(TARGET) record python 10.0
	@./$(TARGET) record cargo  90.0
	@./$(TARGET) record nvim   200.0
	@./$(TARGET) record awk    0.3
	@./$(TARGET) record sed    0.2
	@./$(TARGET) record find   1.1
	@./$(TARGET) record top    5.0
	@./$(TARGET) record htop   12.0
	@echo "データ生成完了 ✓"
	@./$(TARGET) top 10