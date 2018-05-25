
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               tty.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "proto.h"
#include "string.h"
#include "proc.h"
#include "global.h"
#include "keyboard.h"

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define TEXT_SIZE V_MEM_SIZE / 2 //显示屏上的是2字节(前景背景+内容)

PRIVATE u8 *vmem = V_MEM_BASE;//指向显存
PRIVATE u8 vmem_cont[V_MEM_SIZE];//要写入显存的字节数组
PRIVATE char text[TEXT_SIZE];//输出内容的字符数组
PRIVATE int text_pos;//类似指针，指向内容数组的某个位置
PRIVATE int state;//状态描述符：0代表初始状态，1代表初次按下ESC，2代表1状态下按下ENTER
PRIVATE char search[TEXT_SIZE];//1状态下输入的字符内容数组
PRIVATE int search_pos;//类似text_pos作用
PRIVATE int cursor_pos;//光标位置
PRIVATE int caps_lock;//大小写锁定

PRIVATE void sos();//show on screen
PRIVATE char get_really_char();

/*======================================================================*
                           task_tty
 *======================================================================*/
PUBLIC void task_tty()
{
    disp_str("Loading console...\n");
    memset(text, 0, TEXT_SIZE);
    text_pos = state = caps_lock = 0;
    sos();
    while (1) {
        keyboard_read();
    }
}

/*======================================================================*
                           task_clear
 *======================================================================*/
PUBLIC void task_clear()
{
	while (1) {
        milli_delay(200000);
        if (state == 0) {
            memset(text, 0, TEXT_SIZE);
            text_pos = 0;
            sos();
        }
    }
}

/*设置光标位置*/
PRIVATE void set_cursor(unsigned int position)
{
    disable_int();
    out_byte(CRTC_ADDR_REG, CURSOR_H);
    out_byte(CRTC_DATA_REG, (position >> 8) & 0xFF);
    out_byte(CRTC_ADDR_REG, CURSOR_L);
    out_byte(CRTC_DATA_REG, position & 0xFF);
    cursor_pos = position;
    enable_int();
}

PRIVATE void sos()
{
	//先清除vmem_cont的内容，以防显示不必要的字符
	//颜色设置为0x0f，即默认亮白
	int i = 0;
	for (i = 0; i < V_MEM_SIZE / 2; i++) {
		vmem_cont[i * 2] = 0;
		vmem_cont[i * 2 + 1] = 0x0f;//高字节颜色，低字节内容
	}
	int row = 0, col = 0;
	for (i = 0; i < text_pos; i++) {
		if (text[i] == 0) break;
		switch (text[i]) {
		case '\n' :
			row = (row + 1) % SCREEN_HEIGHT;
			col = 0;
			break;
		case '\t' :
			col = col + (4 - col % 4);
			break;
		default:
			vmem_cont[(row * SCREEN_WIDTH + col) * 2] = text[i];
			//state=2情况下，进行搜索，查找到相同的将颜色置为蓝色
			if (state == 2 && text[i] == search[0]) {
				int j = 1;
				int is_same = 1;
				for (j = 1; j < search_pos; j++) {
					if (i + j >= text_pos || text[i + j] != search[j]) {
						is_same = 0;
						break;
					}	
				}
				if (is_same == 1) {
					for (j = 0; j < search_pos; j++) {
						vmem_cont[(row * SCREEN_WIDTH + col + j) * 2 + 1] = 0x0c;
					}
				}
			}
			col++;
		}
		if (col >= SCREEN_WIDTH){
			col = col % SCREEN_WIDTH;
			row = (row + 1) % SCREEN_HEIGHT;
		}
	}
	if (state > 0) {
		for (i = 0; i < search_pos; i++) {
			vmem_cont[(row * SCREEN_WIDTH + col) * 2] = search[i];
			vmem_cont[(row * SCREEN_WIDTH + col) * 2 + 1] = 0x0c;
			col++;
			if (col >= SCREEN_WIDTH) {
				col = col % SCREEN_WIDTH;
				row = (row + 1) % SCREEN_HEIGHT;
			}
		}
	}
	//加载进显存
	memcpy(vmem, vmem_cont, V_MEM_SIZE);
    set_cursor(row * SCREEN_WIDTH + col);
}

PRIVATE char get_really_char(u32 key)
{
	char c = key & 0xFF;
	if (caps_lock == 1) {
		if (c >= 'A' && c <= 'Z') {
			c = c + 'a' - 'A';
		} else if (c >= 'a' && c <= 'z') {
			c = c - ('a' - 'A');
		}
	}
	return c;
}

PUBLIC void in_process(u32 key)
{
    if ((key & FLAG_EXT) && ((key & MASK_RAW) == CAPS_LOCK) && (state != 2)) {
		//只有在state不为2的情况下，大写锁定才会起作用
        caps_lock = 1 - caps_lock;
        while (in_byte(KB_CMD) & 0x02);
        out_byte(KB_DATA, 0xed);
        while (in_byte(KB_DATA) != 0xfa && in_byte(KB_CMD) & 0x02);
        out_byte(KB_DATA, caps_lock << 2);
        while (in_byte(KB_DATA) != 0xfa);
    } else if (state == 0) {
        if (!(key & FLAG_EXT)) {
            text[text_pos] = get_really_char(key);
			text_pos++;
        } else {
            switch(key & MASK_RAW) {
            case ENTER:
                text[text_pos] = '\n';
				text_pos++;
                break;
            case TAB:
                text[text_pos] = '\t';
				text_pos++;
                break;
            case BACKSPACE:
                if (text_pos > 0) {
					text_pos--;
					text[text_pos] = 0;
				}
                break;
            case ESC:
                memset(search, 0, TEXT_SIZE);
                search_pos = 0;
                state = 1;
            }
        }
        text_pos %= TEXT_SIZE;
    } else if (state == 1) {
        if (!(key & FLAG_EXT)) {
            search[search_pos] = get_really_char(key);
			search_pos++;
        } else {
            switch (key & MASK_RAW) {
            case ENTER:
                state = 2;
                break;
            case BACKSPACE:
                if (search_pos > 0) {
					search_pos--;
					search[search_pos] = 0;
				}
                break;
            case ESC:
                state = 0;
            }
        }
    } else if (state == 2) {
		if ((key & FLAG_EXT) && ((key & MASK_RAW) == ESC)) {
			state = 0;
		}
    }
    sos();
}

