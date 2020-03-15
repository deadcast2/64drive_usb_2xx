//
// debug.c
//

#include "usb.h"
#include "upgrade.h"
#include "device.h"
#include "helper.h"
#include "debug.h"

#include "windows.h"

#define MAX_DEBUG_BLOCK (512)

void debug_handle_blocktype_d0_init(ftdi_context_t *c);
void debug_handle_blocktype_d0_callback(ftdi_context_t *c);
void debug_handle_blocktype_d0_cleanup();

void debug_server(debug_files_t *d, ftdi_context_t *c)
{
	int bytes_rx_pending;
	u8 buf[512];
	u8 *block;
	//static u8 block[1*1024*1024];
	u8 block_magic[] = {'D', 'M', 'A', '@'};
	u8 block_type;
	u32 block_length;
	u32 cmp_magic;
	dev_cmd_resp_t r;
	int blocktype_init_done[256] = {0, };

	if (d->debug_enable == 0) return;
	if (c->verbose) _printf("Starting debug server");

	if (!c->handle) die(err[DEV_ERR_NULL_HANDLE], __FUNCTION__);

	block = malloc(MAX_DEBUG_BLOCK);
	if (block == NULL) die("Couldn't malloc block for debug", __FUNCTION__);

	_printf("Debug server started, listening... Press Esc to exit");
	while (1)
	{
		if (GetAsyncKeyState(VK_ESCAPE)) break;
		FT_GetQueueStatus(c->handle, &bytes_rx_pending);
		if (bytes_rx_pending > 0){
			// data is waiting in the receive buffer
			if (c->verbose) _printf("Block pending");

			c->status = FT_Read(c->handle, buf, 4, &c->bytes_read);
			if (memcmp(buf, block_magic, 4) != 0) die("Unexpected DMA header", __FUNCTION__);
			c->status = FT_Read(c->handle, buf, 4, &c->bytes_read);
			block_length = swap_endian(bytes_to_u32(buf));

			// grab the top byte
			block_type = (block_length >> 24) & 0xff;
			// mask off the lower 3 bytes
			block_length &= 0xffffff;
			if (c->verbose) _printf("Receiving block type %d of length %d", block_type, block_length);
			if (block_length > MAX_DEBUG_BLOCK) die("Device tried to send larger block than supported", __FUNCTION__);
			if (block_length % 4 > 0) die("Device tried to send blocksize not 32bit aligned", __FUNCTION__);

			// read the block data
			c->status = FT_Read(c->handle, block, block_length, &c->bytes_read);
			if (c->bytes_read != block_length) die("Couldn't read all of the block", __FUNCTION__);

			// read the CMP signal
			c->status = FT_Read(c->handle, buf, 4, &c->bytes_read);
			cmp_magic = swap_endian(bytes_to_u32(buf));
			if (cmp_magic != 0x434D5048){
				die("Received wrong CMPlete signal", __FUNCTION__);
			}


			// process the block
			switch(block_type) {
			case 0x01:
				// just printf the data directly
				printf(block);
				break;

			case 0xd0:
				// if this is the first time the handler is triggered, call its init
				if (!blocktype_init_done[0x0D]){
					debug_handle_blocktype_d0_init(c);
					blocktype_init_done[0x0D] = 1;
				}
				// pass the received bytes along to the function as well
				debug_handle_blocktype_d0_callback(c, block);
				break;
			}
		
		}
		FT_GetQueueStatus(c->handle, &bytes_rx_pending);
		if (bytes_rx_pending == 0) Sleep(10);
	}

	if (c->verbose) _printf("Stopping blocktype handler inits");
	for (int i = 0; i < 256; i++){
		switch (i){
		case 0x0D:
			// only destroy if it was ever created
			if (blocktype_init_done[i])	debug_handle_blocktype_d0_cleanup();
			break;
		}
	}

	_printf("Debug server stopped");
	if(block != NULL) free(block);
}



//
// below is all the code for the sample1 desktop capture program
//

#define ENDIANFLIP16(x) x = ((x&0xff)<<8) | ((x&0xff00)>>8)
#define	GPACK_RGBA5551(r, g, b, a)	((((r)<<8) & 0xf800) | 		\
					 (((g)<<3) & 0x7c0) |		\
					 (((b)>>2) & 0x3e) | ((a) & 0x1))

POINT		pt;
INPUT		Input = { 0 };
int			ptr;
unsigned short pixel;
unsigned char red, green, blue;
unsigned short *screen_buf;

HDC			hdc;
BITMAPINFO	target_bmp = { sizeof(BITMAPINFOHEADER), 320, -480, 1, 32, BI_RGB };
BITMAPINFO	source_bmp = { sizeof(BITMAPINFOHEADER), 1111, -1111, 1, 32, BI_RGB };

BYTE		*pBits = NULL;

HBITMAP		hbmMem;
HDC			hdcMem;
HBITMAP		hbmOld;

int			desktop_width;
int			desktop_height;

s16			cont_analog_x;
s16			cont_analog_y;
u32			cont_z, cont_z_last;
u32			cont_r, cont_r_last;
u32			cursor_focus = 1;

void debug_handle_blocktype_d0_init(ftdi_context_t *c)
{
	if (c->verbose) _printf("Blocktype 0xD0 handler init");

	hdc = GetDC(NULL);
	desktop_width = GetDeviceCaps(hdc, HORZRES);
	desktop_height = GetDeviceCaps(hdc, VERTRES);

	source_bmp.bmiHeader.biWidth = desktop_width;
	source_bmp.bmiHeader.biHeight = -desktop_height;
	
	hbmMem = CreateDIBSection(NULL, &target_bmp, DIB_RGB_COLORS, (void**)&pBits, NULL, NULL);
	hdcMem = CreateCompatibleDC(hdc);
	hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

	screen_buf = malloc(640 * 480 * 2);

	_printf("PC: debug_handle_blocktype_d0_init()");
	_printf("PC: Mouse buttons: Ztrig left, Rtrig right");
	_printf("PC: Zoom level: C-buttons");
}

void _mouse_button(DWORD dwFlags)
{
	Input.type = INPUT_MOUSE;
	Input.mi.dwFlags = dwFlags;
	SendInput(1, &Input, sizeof(INPUT));
}

void _blit_cursor(POINT pt, int size)
{
	// if the mouse cursor is displaying, draw it
	CURSORINFO cursorInfo = { 0 };
	cursorInfo.cbSize = sizeof(cursorInfo);
	if (GetCursorInfo(&cursorInfo))	{
		ICONINFO ii = { 0 };
		GetIconInfo(cursorInfo.hCursor, &ii);
		DeleteObject(ii.hbmColor);
		DeleteObject(ii.hbmMask);
		DrawIconEx(hdcMem, pt.x - ii.xHotspot, pt.y - ii.yHotspot, cursorInfo.hCursor, size, size, 0, NULL, DI_NORMAL);
	}
}

void debug_handle_blocktype_d0_callback(ftdi_context_t *c, u8 *block_data)
{
	dev_cmd_resp_t r;
	u8 buf[512];
	u32 cmp_magic;

	// tell 64drive usb pipe we are going to be sending a data block with type d0
	device_sendcmd(c, &r, DEV_CMD_USBRECV, 1, 0, 1,
		((320 * 240 * 2) & 0xffffff) | 0xd0 << 24, 0);

	// now send the data portion
	c->status = FT_Write(c->handle, screen_buf, 320 * 240 * 2, &c->bytes_written);

	// 64drive will send back a CMPletion signal like an ack
	c->status = FT_Read(c->handle, buf, 4, &c->bytes_read);
	cmp_magic = swap_endian(bytes_to_u32(buf));
	if (cmp_magic != 0x434D5040){
		die("Received wrong CMPlete signal", __FUNCTION__);
	}


	// adjust cursor position by controller0's analog bytes
	GetCursorPos(&pt);
	cont_analog_x = (signed char)block_data[2];
	cont_analog_y = (signed char)block_data[3];
	// deadzone
	if (abs(cont_analog_x) < 5) cont_analog_x = 0;
	if (abs(cont_analog_y) < 5) cont_analog_y = 0;
	cont_analog_x *= -cont_analog_x * (cont_analog_x < 0 ? 1 : -1);
	cont_analog_y *= -cont_analog_y * (cont_analog_y < 0 ? 1 : -1);
	// move position
	pt.x += cont_analog_x / 100;
	pt.y -= cont_analog_y / 100;
	SetCursorPos(pt.x, pt.y);

	cont_z_last = cont_z;
	cont_r_last = cont_r;
	cont_z = (block_data[0] & 0x20) != 0;
	cont_r = (block_data[1] & 0x10) != 0;

	// simulate a mouse click
	if (cont_z & !cont_z_last) _mouse_button(MOUSEEVENTF_LEFTDOWN);
	if (!cont_z & cont_z_last) _mouse_button(MOUSEEVENTF_LEFTUP);
	if (cont_r & !cont_r_last) _mouse_button(MOUSEEVENTF_RIGHTDOWN);
	if (!cont_r & cont_r_last) _mouse_button(MOUSEEVENTF_RIGHTUP);

	
	if ((block_data[1] & 0x8)) cursor_focus = 0;
	if ((block_data[1] & 0x1)) cursor_focus = 2;
	if ((block_data[1] & 0x4)) cursor_focus = 1;


	if (cursor_focus == 0){
		if (desktop_width < desktop_height) die("Desktop screen size is narrower than tall, aborting", __FUNCTION__);
		int new_width = desktop_height * 4 / 3;
		int new_left = (desktop_width - new_width)/2;

		// stretch part of the desktop to our target bitmap
		SetStretchBltMode(hdcMem, HALFTONE);
		StretchBlt(hdcMem,
			0, 0,		// upper left corner of destination (always 0,0)
			320, 240,	// destination bitmap size
			hdc,
			new_left, 0,		// upper left corner of desktop to capture from
			new_width, desktop_height, // desktop capture width and height
			SRCCOPY);
		// find cursor position now
		pt.x -= 240;
		pt.x = pt.x * 320 / 1440;
		pt.y -= 0;
		pt.y = pt.y * 240 / 1080;
		_blit_cursor(pt, 24);
	}
	else if(cursor_focus == 1){
		// 320x240 direct capture
		BitBlt(hdcMem, 0, 0, 320, 240, hdc, pt.x - 160, pt.y - 120, SRCCOPY);
		POINT cursor_point = { 160, 120 };
		_blit_cursor(cursor_point, 32);
	} else if (cursor_focus == 2){
		// 640x480 zoom
		SetStretchBltMode(hdcMem, HALFTONE);
		StretchBlt(hdcMem,
			0, 0,		// upper left corner of destination (always 0,0)
			320, 240,	// destination bitmap size
			hdc,
			pt.x - 320, pt.y - 240,		// upper left corner of desktop to capture from
			640, 480, // desktop capture width and height
			SRCCOPY);

		// find cursor position now
		pt.x = 160;
		pt.y = 120;

		_blit_cursor(pt, 24);
	}
	GdiFlush();

	// convert 32bit RGBA framebuffer to 16bit 5551, big endian
	ptr = 0;
	for (int i = 0; i < (320 * 4 * 240); i += 4){
		red = pBits[i + 2];
		green = pBits[i + 1];
		blue = pBits[i + 0];

		pixel = GPACK_RGBA5551(red, green, blue, 255);
		ENDIANFLIP16(pixel);
		screen_buf[ptr] = pixel;
		ptr++;
	}

}

void debug_handle_blocktype_d0_cleanup()
{
	// more GDI stuff should be done here, but oh well
	free(screen_buf);
}