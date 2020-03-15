//
// image.c
//

#include "usb.h"
#include "image.h"
#include "device.h"
#include "helper.h"
#include <io.h>

static u8		buffer[CHUNK_SIZE];


void image_load(game_files_t *g, ftdi_context_t *c)
{
	int		i;
	int		cnt = 0;
	FILE	*fp;

	for(i = 0; i < g->num_files; i++) {
		if(g->files[i][0]) {
			// load file
			fp = fopen(g->files[i], g->dump ? "wb" : "rb");
			if(fp == NULL) die(err[DEV_ERR_NULL_FILE], __FUNCTION__);
			if(!g->dump){
				// get file size
				fseek(fp, 0L, SEEK_END);
				g->sizes[i] = ftell(fp);
				fseek(fp, 0L, SEEK_SET);
			}

			// print status
			_printf("%s %s %s %s at 0x%x (%d %s) %s", 
				g->dump ? "Dumping" : "Loading", g->files[i], g->dump ? "from" : "into", 
				bank_desc[g->types[i]], g->addrs[i], (g->sizes[i] < 1024) ? g->sizes[i] : g->sizes[i] / 1024, (g->sizes[i] < 1024) ? "b" : "kb", g->standalone ? "in real cart": "");

			if(g->standalone){
				if(g->types[i] == BANK_CARTROM && !g->dump) die("Can't write to real cartridge!", __FUNCTION__);

				if(g->types[i] == BANK_CARTROM){
					// use existing transfer function for large cartrom (PI)
					//g->addrs[i] += 0x10000000;
					image_transfer(fp, c, g->dump, g->types[i], 1, 512, g->addrs[i], g->sizes[i]);
				} else {
					// use specialized whole-area save chip function
					if(g->addrs[i] != 0x0) die("Save banks in standalone mode must be addressed 0x0", __FUNCTION__);
					image_transfer_save(fp, c, g->dump, g->types[i], g->sizes[i]);
				}

			} else {
				image_transfer(fp, c, g->dump, g->types[i], 0, 0, g->addrs[i], g->sizes[i]);
				if (g->sizes[i] > 64 * 1024 * 1024){
					// image is bigger than 64MByte
					_printf("Setting extended CI addressing, image > 64mbyte");
					image_set_ciext(c, 1);
				}
			}

			if(g->sizes[i] < 1052672 && g->types[i] == BANK_CARTROM && g->addrs[i] == 0 && !g->dump){
				// sanity check 
				_printf("Image is smaller than 1028Kbyte, will probably fail boot CRC.");
				_printf("Please pad the image out to 1028KB.");
			}
			fclose(fp);
			cnt++;
		}
		if(g->save_types[i] > 0) {
			// set save type
			_printf("Setting save type to %s", save_desc[g->save_types[i]-1]);
			image_set_save(c, g->save_types[i] - 1);
		}
		if (g->cic_types[i] > 0) {
			// set cic type
			int cic = g->cic_types[i] - 1;

			if (cic == 6101) cic = 0;
			if (cic == 6102) cic = 1;
			if (cic == 7101) cic = 2;
			if (cic == 7102) cic = 3;
			if (cic == 6103 || cic == 7103) cic = 4;
			if (cic == 6105 || cic == 7105) cic = 5;
			if (cic == 6106 || cic == 7106) cic = 6;
			if (cic == 5101) cic = 7;

			if (cic >= CIC_LAST) _printf("Requested CIC type does not exist, skipping");
			else {
				_printf("Setting CIC type to %s", cic_desc[cic]);
				image_set_cic(c, cic);
			}
		}
	}
	if(c->verbose) _printf(info[INFO_TOTALDONE], cnt);
}

void image_transfer(FILE *fp, ftdi_context_t *c, u8 dump, u8 type, u8 standalone, u32 burst_size, u32 addr, u32 size)
{	
	u32		ram_addr = addr;
	int		bytes_left = size;
	int		bytes_done = 0;
	int		bytes_do;
	int		trunc_flag = 0;
	int		i; 
	int		j;
	int		chunk = 0;
	LARGE_INTEGER	time_start;
	LARGE_INTEGER	time_stop;
	LARGE_INTEGER	time_freq;
	LONGLONG		time_diff;
	double			time_duration;
	dev_cmd_resp_t	r;
  
	// make sure handle is valid
	if(!c->handle) die(err[DEV_ERR_NULL_HANDLE], __FUNCTION__);

	// decide a better, more optimized chunk size
	if(size > 16 * 1024 * 1024) 
		chunk = 32;
	else if( size > 2 * 1024 * 1024)
		chunk = 16;
	else 
		chunk = 4;
	// convert to megabytes
	chunk *= 128 * 1024;

	if(standalone) chunk /= 2;
	if(c->verbose) _printf(info[INFO_CHUNK], CHUNK_SIZE);
	if(c->verbose) _printf(info[INFO_OPT_CHUNK], chunk);
	/*
	if (standalone) {
		FT_SetUSBParameters(c->handle, 1024, 0);
		FT_SetLatencyTimer(c->handle, 2);
	}
	else {
		FT_SetUSBParameters(c->handle, 32768, 0);
		FT_SetLatencyTimer(c->handle, 16);
	}*/
	

	// enter standalone mode?
	if(standalone){
		if(c->verbose) _printf("Entering standalone mode");
		device_sendcmd(c, &r, DEV_CMD_STD_ENTER, 0, 0, 0, 0, 0);
	}

	// get initial time count
	QueryPerformanceFrequency(&time_freq);
	QueryPerformanceCounter(&time_start);

	prog_draw(0, size);
	while(1){
		if(bytes_left >= chunk) 
			bytes_do = chunk;
		else
			bytes_do = bytes_left;
		if(bytes_do % 4 != 0 && !standalone) {
			trunc_flag = 1;
			bytes_do -= (bytes_do % 4);
		}
		if(bytes_do <= 0) break;
		
		for(i = 0; i < 2; i++){
			if(i == 1) {
				printf("\n");
				_printf("Retrying\n");

				FT_ResetPort(c->handle);
				FT_ResetDevice(c->handle);
				_printf("Retrying FT_ResetDevice() success\n");
				// set synchronous FIFO mode
				//FT_SetBitMode(c->handle, 0xff, 0x40);
				_printf("Retrying FT_SetBitMode() success\n");
				FT_Purge(c->handle, FT_PURGE_RX | FT_PURGE_TX);
				_printf("Retrying FT_Purge() success\n");
			}

			if(standalone){
				// operate on real attached cartridge
				for(j = 0; j < bytes_do/burst_size; j++){
					device_sendcmd(c, &r, DEV_CMD_PI_RD_BURST, 2, 0, 1, ram_addr+j*burst_size, burst_size/4);
					c->status = FT_Read(c->handle, &buffer[j*burst_size], burst_size, &c->bytes_written);
					if (c->bytes_written != burst_size) die("Wrong datasize read", __FUNCTION__);
				}
				fwrite(buffer, bytes_do, 1, fp);
			} else {
				// operate on internal cart emulator
				device_sendcmd(c, &r, dump ? DEV_CMD_DUMPRAM : DEV_CMD_LOADRAM, 2, 0, 1, 
					ram_addr, (bytes_do & 0xffffff) | type << 24);

				if(dump){
					c->status = FT_Read(c->handle, buffer, bytes_do, &c->bytes_written);
					fwrite(buffer, bytes_do, 1, fp);
				}else{
					fread(buffer, bytes_do, 1, fp);
					c->status = FT_Write(c->handle, buffer, bytes_do, &c->bytes_written);
				}
			}

			if(c->bytes_written) break;
		}
		// check for a timeout
		if(c->bytes_written == 0) die(err[DEV_ERR_TIMED_OUT], __FUNCTION__);
		// dump success response
		if(!standalone) c->status = FT_Read(c->handle, buffer, 4, &c->bytes_read);

		bytes_left -= bytes_do;
		bytes_done += bytes_do;
		ram_addr += bytes_do;

		// progress bar
		prog_draw(bytes_done, size);

		c->status = FT_GetStatus(c->handle, &c->bytes_read, &c->bytes_written, &c->event_status);
	}
	// stop the timer
	QueryPerformanceCounter(&time_stop);
	time_diff = time_stop.QuadPart - time_start.QuadPart;
	// get the difference of the timer
	time_duration = ((double) time_diff * 1000.0 / (double) time_freq.QuadPart) / 1000.0f;
	// erase progress bar
	prog_erase();
	if(c->verbose && trunc_flag) 
		_printf(info[INFO_TRUNCATED]);
	if(c->verbose) 
		_printf(info[INFO_COMPLETED_TIME],	time_duration, (float)size/1048576.0f/(float)time_duration);

	if(standalone){
		device_sendcmd(c, &r, DEV_CMD_STD_LEAVE, 0, 0, 0, 0, 0);
	}
}

void image_transfer_save(FILE *fp, ftdi_context_t *c, u8 dump, u8 type, u32 size)
{
	 // snipped from this source code release.
	 // please use UltraSave as its flash code is highly superior
	 // if you still need to look at this, email me
	 _printf("image_transfer_save NOT IMPLEMENTED");
}

void image_set_save(ftdi_context_t *c, u8 save_type)
{	
	dev_cmd_resp_t r;
	device_sendcmd(c, &r, DEV_CMD_SETSAVE, 1, 0, 0, save_type, 0);
}

void image_set_cic(ftdi_context_t *c, u8 cic_type)
{
	dev_cmd_resp_t r;
	if (c->variant[0] == 'A'){
		_printf("Your hardware does not support changing CIC emulation.");
		_printf("CIC remains whatever was installed in your unit - make sure ROM header matches.");
		return;
	}
	// bit 31 set = tell CIC to change mode
	// bit 31 not set = let CIC be its default
	device_sendcmd(c, &r, DEV_CMD_SETCIC, 1, 0, 0, (1 << 31) | cic_type, 0);
}

void image_set_ciext(ftdi_context_t *c, u8 extended)
{
	dev_cmd_resp_t r;
	if (c->variant[0] == 'A'){
		_printf("Your hardware does not support extended CI address mode.");
		_printf("Image data past 64mbyte will be truncated.");
		return;
	}
	if (c->firmware < 206){
		_printf("Failed - upgrade firmware to at least 2.06 for CI extended addressing");
		return;
	}
	// bit 0 set = extended addressing to use full 240mbyte on HW2
	// bit 0 not set = only 64mbyte is mapped
	device_sendcmd(c, &r, DEV_CMD_SETCIEXT, 1, 0, 0, extended > 0, 0);
}

