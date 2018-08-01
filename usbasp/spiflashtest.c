
#include <stdio.h>

// uint types
#include <unistd.h>

// malloc
#include <stdlib.h>

// memcpy
#include <memory.h>

// file handling
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// USB
#include <libusb-1.0/libusb.h>

static struct libusb_device_handle *device_handle = NULL;
uint8_t libusb_initialized = 0, interface_claimed = 0;

void print_progress_bar (size_t done, size_t total)
{
    const char *PBSTR = "#################################################";
    if(total == 0 || done > total)
      done = total = 1; // avoid division by zero
    const size_t PBWIDTH = strlen(PBSTR);
    int percent = (int) (100 * done / total);
    int lpad = (int) (PBWIDTH * done / total);
    int rpad = PBWIDTH - lpad;
    fprintf(stderr, "\r%3d%% [%.*s%*s]", percent, lpad, PBSTR, rpad, "");
    fflush(stderr);
}

void print_hex_buf(uint8_t *buf, size_t len)
{
  for(int i = 0; i < len; i++)
  {
      if(i % 32 == 0 && i != 0)
        printf("\n");
      printf("%02X ", buf[i]);
  }
  printf("\n");
}


void cmd_addr(uint8_t *buf, uint8_t cmd, uint32_t addr)
{
  buf[0] = cmd;
  buf[1] = 0xFF & (addr >> 16);
  buf[2] = 0xFF & (addr >> 8);
  buf[3] = 0XFF & addr;
}

// up to 32 byte single packet in/out exchange
int txrx(uint8_t *out_data, size_t out_len, uint8_t *in_data, size_t in_len)
{
  uint8_t bRequest = 0; // currently no use
  uint16_t wIndex = 0; // currently no use
  uint16_t wValue = 0; // wValue: 0-no continuation, 1-continuation
  uint16_t timeout_ms = 10; // 10 ms waiting for response
  int response;

  response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_OUT|LIBUSB_REQUEST_TYPE_VENDOR),
      bRequest, wValue, wIndex, out_data, out_len, timeout_ms);
  if(response < 0)
  {
    fprintf(stderr, "txrx OUT: %s\n", libusb_error_name(response));
    return -1; // something went wrong with USB
  }

  if(in_data == NULL || in_len == 0)
    return 0;

  response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR),
      bRequest, wValue, wIndex, in_data, in_len, timeout_ms);
  if(response < 0)
  {
    fprintf(stderr, "txrx IN: %s\n", libusb_error_name(response));
    return -1; // something went wrong with USB
  }

  return 0;
}

int flash_read_id()
{
  uint8_t buf[5];
  cmd_addr(buf, 0xAB, 0);
  int rc = txrx(buf, sizeof(buf), buf, sizeof(buf));
  if(rc < 0)
    return rc;
  return buf[4];
}

int flash_read_status()
{
  uint8_t buf[2];
  buf[0] = 0x05;
  int rc = txrx(buf, sizeof(buf), buf, sizeof(buf));
  if(rc < 0)
    return rc;
  return buf[1];
}

int flash_wait_while_busy()
{
  while(flash_read_status() & 1);
}

int flash_write_enable()
{
  uint8_t buf[1];
  buf[0] = 0x06;
  int rc = txrx(buf, sizeof(buf), NULL, 0);
  if(rc < 0)
    return rc;
  return 0;
}

int flash_write_disable()
{
  uint8_t buf[1];
  buf[0] = 0x04;
  int rc = txrx(buf, sizeof(buf), NULL, 0);
  if(rc < 0)
    return rc;
  return 0;
}


int flash_read(uint8_t *data, size_t addr, size_t length)
{
  uint8_t buf[32]; // USB I/O buffer
  size_t accumulated_read = 0; // accumulate total read
  size_t payload_start = 4; // initial payload starts at byte 4 without continuation
  uint8_t data1 = 0; // currently no use
  uint8_t bRequest = 0; // currently no use
  uint16_t wIndex = 0; // currently no use
  uint16_t wValue = length <= sizeof(buf)-payload_start ? 0 : 1; // wValue: 0-no continuation, 1-continuation
  uint16_t timeout_ms = 10; // 10 ms waiting for response

  cmd_addr(buf, 0x03, addr); // FLASH normal (slow) read
  
  while(accumulated_read < length)
  {
    int response;

    #if 0
    // IN request - wait for SPI to finish its transmission 
    buf[0] = 1;
    while(buf[0] == 1)
    {
      response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR),
        1, 0, 0, buf, 1, timeout_ms);
      if(buf[0])
        printf("spi busy before out %d\n", buf[0]);
    }
    #endif

    // write to USB read command followed with dummy bytes
    // in order to read, we must first write command and the
    // contiue writing anything to SPI
    // every written byte will also result in reading a byte.
    // up to 32 read bytes are buffered inside of the USB device.
    // this USB buffer can be retrieved by subsequent IN command later.
    response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_OUT|LIBUSB_REQUEST_TYPE_VENDOR|data1),
      bRequest, wValue, wIndex, buf, sizeof(buf), timeout_ms);
    if(response < 0)
    {
      fprintf(stderr, "OUT: %s\n", libusb_error_name(response));
      return -1; // something went wrong with USB
    }
    // calculate next request length (how much to read from USB)
    size_t request_size;
    if(accumulated_read + sizeof(buf) - payload_start >= length)
    {
      // printf("last packet\n");
      // end packet, trim request size to how much we really need
      request_size = length + payload_start - accumulated_read;
      wValue = 0; // terminate continuation
    }
    else
      request_size = sizeof(buf);

    #if 0
    // IN request - wait for SPI to finish its transmission 
    buf[0] = 1;
    while(buf[0] == 1)
    {
      response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR),
        1, 0, 0, buf, 1, timeout_ms);
      if(buf[0])
        printf("spi busy before in %d\n", buf[0]);
    }
    #endif

    //usleep(1000000);
    // usleep(11); // sleep 11us for SPI to tranfer (usually not needed)
    response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR|data1),
      bRequest, wValue, wIndex, buf, request_size, timeout_ms);
    if(response != request_size)
    {
      fprintf(stderr, "IN: %s\n", libusb_error_name(response));
      return -1; // something went wrong with USB
    }
    size_t response_size = response - payload_start;
    memcpy(data, buf+payload_start, response_size);
    data += response_size;
    accumulated_read += response_size;
    if(payload_start) // contination will result in full 32-byte payload
      payload_start = 0;
  }
  return 0; // 0 on success
}

// retry max "retry" times until 'match' of consecutive identical readings appear
int flash_read_retry(uint8_t *data, size_t addr, size_t length, int retry, int match)
{
}

// only 3 selected sector lengths are possible
int flash_erase_sector(size_t addr, size_t len)
{
  uint8_t opcode = 0; // null-opcode is NOP
  if(len ==  4*1024) opcode = 0x20;
  if(len == 32*1024) opcode = 0x52;
  if(len == 64*1024) opcode = 0xd8;
  if(opcode == 0)
    return -1; // unsupported length
  flash_write_enable();
  uint8_t buf[4];
  cmd_addr(buf, opcode, addr);
  int rc = txrx(buf, sizeof(buf), NULL, 0);
  if(rc < 0)
    return -1; // error in txrx
  flash_wait_while_busy();
}

int flash_write(uint8_t *data, size_t addr, size_t length)
{
  uint8_t buf[32]; // USB I/O buffer
  size_t accumulated_write = 0; // accumulate total read
  size_t payload_start = 4; // initial payload starts at byte 4 without continuation
  uint8_t data1 = 0; // currently no use
  uint8_t bRequest = 0; // currently no use
  uint16_t wIndex = 0; // currently no use
  uint16_t wValue = length <= sizeof(buf)-payload_start ? 0 : 1; // wValue: 0-no continuation, 1-continuation
  uint16_t timeout_ms = 10; // 10 ms waiting for response

  flash_write_enable();

  cmd_addr(buf, 0x02, addr); // FLASH write (should be previous erased to 0xFF)
  while(accumulated_write < length)
  {
    int response;

    #if 0
    // IN request - wait for SPI to finish its transmission 
    buf[0] = 1;
    while(buf[0] == 1)
    {
      response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR),
        1, 0, 0, buf, 1, timeout_ms);
      if(buf[0])
        printf("spi busy before out %d\n", buf[0]);
    }
    #endif

    // calculate next request length (how much to read from USB)
    size_t request_size;
    if(accumulated_write + sizeof(buf) - payload_start >= length)
    {
      // printf("last packet\n");
      // end packet, trim request size to how much we really need
      request_size = length + payload_start - accumulated_write;
      wValue = 0; // terminate continuation
    }
    else
      request_size = sizeof(buf);
    size_t payload_size = sizeof(buf) - payload_start;
    // printf("paystart %d, payload_size %d\n", payload_start, payload_size);
    memcpy(buf+payload_start, data, payload_size);

    // write to USB the flash write command followed with data
    response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_OUT|LIBUSB_REQUEST_TYPE_VENDOR|data1),
      bRequest, wValue, wIndex, buf, request_size, timeout_ms);
    if(response < 0)
    {
      fprintf(stderr, "OUT: %s\n", libusb_error_name(response));
      return -1; // something went wrong with USB
    }

    #if 0
    // IN request - wait for SPI to finish its transmission 
    buf[0] = 1;
    while(buf[0] == 1)
    {
      response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR),
        1, 0, 0, buf, 1, timeout_ms);
      if(buf[0])
        printf("spi busy before in %d\n", buf[0]);
    }
    #endif

    data += payload_size;
    accumulated_write += payload_size;
    if(payload_start) // contination will result in full 32-byte payload
      payload_start = 0;

  }
  flash_wait_while_busy();
  return 0; // 0 on success
}


// read from addr, length bytes and write to file
int read_flash_write_file(char *filename, size_t addr, size_t length)
{
  // printf("reading\n");
  const int bufsize = 28; // not much speed improvement in increasing this
  uint8_t buf[2][bufsize]; // 2 buffers, both must match
  size_t accumulated_read = 0;
  int file_descriptor = open(filename, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
  const int retry = 1000;
  size_t next_progress = 0, progress_step = length / 100;
  while(accumulated_read < length)
  {
    int match; // repeat reading until 2 subsequent readings match
    int ib = 0; // buffer index 0/1 to match
    size_t requested_size = accumulated_read + bufsize >= length ? length - accumulated_read : bufsize;
    match = 0;
    const int match_required = 2;
    // printf("accumulated_read %d\n", accumulated_read);
    for(int i = 0; i < retry && match < match_required; i++)
    {
      buf[ib][0] = ~buf[ib^1][0]; // damage first byte for the match to initially fail unless read correct
      buf[ib][requested_size-1] = ~buf[ib^1][requested_size-1]; // damage first byte for the match to initially fail unless read correct
      int rc = flash_read(buf[ib], addr, requested_size);
      if(rc == 0 && memcmp(buf[ib], buf[ib^1], requested_size) == 0)
        match++;
      else
      {
        match = 0;
        if(i > 0)
          printf("read verify error %d\n", i);
      }
      ib ^= 1; // switch buffer
    }
    if(match < match_required)
    {
      fprintf(stderr, "failure after %d retries\n", retry);
      return -1;
    }
    write(file_descriptor, buf[0], requested_size);
    accumulated_read += requested_size;
    addr += requested_size;
    if(accumulated_read > next_progress)
    {
      print_progress_bar(accumulated_read, length);
      next_progress += progress_step;
    }
  }
  print_progress_bar(accumulated_read, length);
  fprintf(stderr, "\n");
  close(file_descriptor);
  return 0;
}

/* JUNK CODE */
  #if 0
  /*
  // PIPE: file length is not known in advance
  size_t file_length = lseek(file_descriptor, 0, SEEK_END);
  lseek(file_descriptor, 0, SEEK_SET); // rewind
  printf("file length %d\n", file_length);
  if(file_length < length)
    length = file_length;
  */
  #endif

  #if 0
  /*
  int sector_map_len = length/available_sector_size[0]+1; // max number of sectors
  uint8_t *sector_map = (uint8_t *) malloc(sector_map_len * sizeof(uint8_t)); // sector erase map
  memset(sector_map, 0, sector_map_len);
  size_t sector_map_base = addr - (addr % available_sector_size[0]);
  // 0 - skip this sector, correct data already there
  // 1 - sector doesn't have to be erased but has to be written
  // 4,32,64 - sector has to be erased but not written
  // 5,33,65 - sector has to be erased and written
  
  //printf("sector map base 0x%06X\n", sector_map_base);
  //print_hex_buf(sector_map, sector_map_len);
  
  // compare flash to file, set sector erase map
  size_t accumulated_read = 0;
  size_t read_addr = addr;
  // calculate how much to read until next sector
  size_t read_to_end_of_sector = sector_map_base + available_sector_size[0] - addr;
  // printf("read_to_end_of_sector 0x%06x-0x%06x, size %d\n", addr, addr+read_to_end_of_sector-1, read_to_end_of_sector);
  int sector_num = 0;
  if(1)
  while(accumulated_read < length)
  {
    if(accumulated_read + read_to_end_of_sector >= length)
      read_to_end_of_sector = length - accumulated_read;
    printf("read_to_end_of_sector 0x%06X-0x%06X, size %d sector %d/%d\n",
      read_addr, read_addr+read_to_end_of_sector-1, read_to_end_of_sector, sector_num, sector_map_len);
    uint8_t *file_byte = flash_sector_buf;
    read(file_descriptor, file_byte, read_to_end_of_sector);
    uint8_t *flash_byte = flash_sector_buf+available_sector_size[0];
    flash_read(flash_byte, read_addr, read_to_end_of_sector);
    for(int i = 0; i < read_to_end_of_sector; i++) // compare every byte
    {
      if( (*flash_byte & *file_byte) != *file_byte)
        sector_map[sector_num] |= 4; // must erase 4K (will become 0xFF after erase)
      if( *flash_byte != *file_byte && *file_byte != 0xFF)
        sector_map[sector_num] |= 1; // must write data
      flash_byte++;
      file_byte++;
    }
    read_addr += read_to_end_of_sector;
    accumulated_read += read_to_end_of_sector;
    read_to_end_of_sector = available_sector_size[0];
    sector_num++;
  }
  lseek(file_descriptor, 0, SEEK_SET); // rewind
  printf("sector map base 0x%06X\n", sector_map_base);
  print_hex_buf(sector_map, sector_map_len);
  // join consecutive 4K sectors into 32K or 64K
  // calculate which small sector relate to larger sector boundary
  size_t sector_32K_map_base = sector_map_base - (sector_map_base % available_sector_size[1]);
  int first_32K_sector = (sector_32K_map_base - sector_map_base) / available_sector_size[0];
  size_t sector_64K_map_base = sector_map_base - (sector_map_base % available_sector_size[2]);  
  int first_64K_sector = (sector_64K_map_base - sector_map_base) / available_sector_size[0];
  printf("first 32K sector %d 0x%06X\n", first_32K_sector, sector_32K_map_base);
  printf("first 64K sector %d 0x%06X\n", first_64K_sector, sector_64K_map_base);
  // join 8 sectors of 4K to 1 of 32K
  int num_sectors_to_join = 8;
  for(int i = first_32K_sector; i < sector_map_len-num_sectors_to_join; i += num_sectors_to_join)
  {
    if(i >= 0)
    {
      int consecutive_sector_count = 0;
      for(int j = 0; j < num_sectors_to_join; j++)
        if((sector_map[i+j] & 4) == 4)
          consecutive_sector_count++;
      if(consecutive_sector_count == num_sectors_to_join)
      {
        sector_map[i] |= 32; // set 32K erase
        for(int j = 0; j < num_sectors_to_join; j++)
          sector_map[i+j] &= ~4; // remove 4K erase
      }
    }
  }
  // join 2 sectors of 32K to 1 of 64K
  num_sectors_to_join = available_sector_size[2]/available_sector_size[0];
  for(int i = first_64K_sector; i < sector_map_len-num_sectors_to_join; i += num_sectors_to_join)
  {
    if(i >= 0)
    {
      if(sector_map[i] == 32 && sector_map[i+num_sectors_to_join/2] == 32)
      {
        sector_map[i] |= 64; // set 64K erase
        sector_map[i] &= ~32; // remove 32K erase
        sector_map[i+num_sectors_to_join/2] &= ~32; // remove 32K erase
      }
    }
  }
  printf("sector map base 0x%06X\n", sector_map_base);
  print_hex_buf(sector_map, sector_map_len);
  free(sector_map);
  */
  #endif

    #if 0
    /*
    size_t restore_begin_len = addr - erase_sector_addr;
    size_t restore_end_len = erase_sector_addr+sector_size - (addr+data_bytes_to_write);
    size_t restore_end_addr = erase_sector_addr+sector_size-restore_end_len;
    printf("erase sector 0x%06X-0x%06X (size %d, restore begin %d, restore end %d) data 0x%06X-0x%06X\n",
      erase_sector_addr,
      erase_sector_addr+sector_size-1,
      sector_size, restore_begin_len, restore_end_len,
      addr, addr+data_bytes_to_write-1); 
    if(restore_begin_len > 0)
    {
      // printf("restore begin 0x%06X-0x%06X\n", erase_sector_addr, erase_sector_addr+restore_begin_len-1);
      // TODO read with retry-verify
      // flash_read(flash_sector_buf, erase_sector_addr, restore_begin_len);
      // print_hex_buf(flash_sector_buf, restore_begin_len);
    }
    if(restore_end_len > 0)
    {
      // printf("restore end 0x%06X-0x%06X\n", restore_end_addr, restore_end_addr+restore_end_len-1);
      // TODO read with retry-verify
      // flash_read(flash_sector_buf+sector_size-restore_end_len, restore_end_addr, restore_end_len);
      // print_hex_buf(flash_sector_buf+sector_size-restore_end_len, restore_end_len);
    }
    // erase sector here (erase_sector_addr, sector_size) (verify if erased to 0xFF)
    // read data from file and write to buffer (flash_sector_buf + addr - erase_sector_addr, data_bytes_to_write);
    // size_t bytes_from_file = read(file_descriptor, flash_sector_buf + addr - erase_sector_addr, data_bytes_to_write);
    // printf("sector to write\n");
    // print_hex_buf(flash_sector_buf, sector_size);
    // write sector with retry-verify (addr, sector_size)
    */
    #endif



// write that many bytes found or file or if file is larger, limit by length
// construct a map of sectors, each byte represents one 4k sector
// map byte value represents erase size in KB 4,32,64
// compare map with file data to find which sector must be erased
// sector must be erased if any bit changes from 0 to 1 set erase value to 4KB
// collect multiple 4K erase sectors into 32K or 64K

int read_file_write_flash(char *filename, size_t addr, size_t length)
{
  const size_t available_sector_size[] = {4*1024, 32*1024, 64*1024}; // sizes in ascending order
  const int num_available_sector_size = sizeof(available_sector_size)/sizeof(available_sector_size[0]);
  uint8_t flash_sector_buf[available_sector_size[num_available_sector_size-1]]; // allocate buf, max sector size
  uint8_t file_sector_buf[available_sector_size[num_available_sector_size-1]]; // allocate buf, max sector size
  int file_descriptor = open(filename, O_RDONLY);
  if(file_descriptor < 0)
    return -1; // cant't open file

  // **** sector logic ****
  // we need to interated over flash sectors
  // if writing to partial sector we first read old data from the sector,
  // erase whole sector, write from file and write old data, then verify and retry 
  const int retry = 10;
  size_t bytes_written = 0;
  int retries_remaining = retry;
  
  printf("writing range limit 0x%06X-0x%06X\n", addr, addr+length-1);
  
  size_t last_read_from_file = 1;
  while(bytes_written < length && last_read_from_file > 0)
  {
    size_t length_remaining = length - bytes_written;
    // find suitable sector to erase
    // 1. priority is to minimize easeing part of data we don't have to erase
    // 2. maximize sector size
    size_t sector_size = available_sector_size[0]; // minimal sector size
    size_t sector_part_before_data = addr % sector_size; // start as minimal sector
    // find do we have any larger  
    if(0) // disabled
    for(int i = 1; i < num_available_sector_size; i++)
    {
      if( addr % available_sector_size[i] == sector_part_before_data // if part before is the same
      && (length_remaining >= available_sector_size[i]-available_sector_size[0])) // and we have enough data
        sector_size = available_sector_size[i]; // accept new sector size
    }
    size_t data_bytes_to_write = sector_size - sector_part_before_data;
    if(bytes_written + data_bytes_to_write >= length)
      data_bytes_to_write = length - bytes_written; // last sector, clamp size
    size_t erase_sector_addr = addr-sector_part_before_data;

    // read sector before erase and before the file
    flash_read(flash_sector_buf, erase_sector_addr, sector_size);
    // copy to file sector (as file may be read in less than sector size)
    memcpy(file_sector_buf, flash_sector_buf, sector_size);

    // data_bytes_to_write is what we want to write, but file may contain less
    // try to read from file "data_bytes_to_write" or get eof:
    size_t remaining_to_read = data_bytes_to_write;
    uint8_t *file_data_pointer = file_sector_buf + addr - erase_sector_addr;
    last_read_from_file = 1;
    while(remaining_to_read > 0 && last_read_from_file > 0)
    {
      last_read_from_file = read(file_descriptor, file_data_pointer, remaining_to_read);
      if(last_read_from_file > 0)
      {
        remaining_to_read -= last_read_from_file;
        file_data_pointer += last_read_from_file;
      }
    }
    size_t actual_bytes_from_file = data_bytes_to_write - remaining_to_read;
    printf("actual_bytes_from_file %d\n", actual_bytes_from_file);
    if(last_read_from_file <= 0)
      printf("****** EOF *******\n");
    // update number of bytes to write
    data_bytes_to_write = actual_bytes_from_file;
    
    // determine do we have to 2:erase, 1:write or 0:leave  the sector unmodified
    // compare byte-by-byte flash_sector_buf and file_sector_buf
    uint8_t must_erase = 0;
    uint8_t must_write = 0;
    for(size_t i = 0; i < sector_size; i++)
    {
      if( (flash_sector_buf[i] & file_sector_buf[i]) != file_sector_buf[i])
        must_erase = 1;
      if( flash_sector_buf[i] != file_sector_buf[i] && file_sector_buf[i] != 0xFF)
        must_write = 1;
    }
    printf("sector 0x%06X-0x%06X (size %d, erase %d, write %d)\n",
      erase_sector_addr,
      erase_sector_addr+sector_size-1,
      sector_size, 
      must_erase, must_write);
    if(must_erase)
      flash_erase_sector(erase_sector_addr, sector_size);
    const size_t page_program_size = 256; // up to this bytes max in one page write operation
    if(must_write)
      for(int i = 0; i < sector_size; i += page_program_size)
        flash_write(file_sector_buf + i, erase_sector_addr + i, page_program_size);
    //printf("file sector written\n");
    //print_hex_buf(file_sector_buf, sector_size);
    // verify
    // read sector before erase and before the file
    flash_read(flash_sector_buf, erase_sector_addr, sector_size);
    //printf("flash sector readback\n");
    //print_hex_buf(flash_sector_buf, sector_size);
    int verify_result = memcmp(flash_sector_buf, file_sector_buf, sector_size);
    if(verify_result == 0)
      printf("VERIFY OK\n");
    else
      printf("VERIFY FAIL\n");

    bytes_written += data_bytes_to_write; // not correct but OK for now
    addr += data_bytes_to_write;
  }
}


static void print_devs(libusb_device **devs)
{
	libusb_device *dev;
	int i = 0, j = 0;
	uint8_t path[8]; 

	while ((dev = devs[i++]) != NULL) {
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(dev, &desc);
		if (r < 0) {
			fprintf(stderr, "failed to get device descriptor");
			return;
		}

		printf("%04x:%04x (bus %d, device %d)",
			desc.idVendor, desc.idProduct,
			libusb_get_bus_number(dev), libusb_get_device_address(dev));

		r = libusb_get_port_numbers(dev, path, sizeof(path));
		if (r > 0) {
			printf(" path: %d", path[0]);
			for (j = 1; j < r; j++)
				printf(".%d", path[j]);
		}
		printf("\n");
//		if(desc.idVendor == 0x16C0 && desc.idProduct == 0x05DC)
//		  vendorspecific();
	}
}

void close_usb_device(void)
{
  printf("aaaa\n");
  if(interface_claimed)
  {
    libusb_release_interface(device_handle, 0);
    interface_claimed = 0;
  }
  if(libusb_initialized)
  {
    libusb_exit(NULL);
    libusb_initialized = 0;
  }
}

int open_usb_device(uint16_t vid, uint16_t pid)
{
  int r = libusb_init(NULL);
  if (r < 0)
  {
    fprintf(stderr, "Cannot init libusb\n");
    close_usb_device();
    return -1;
  }
  libusb_initialized = 1;

  device_handle = libusb_open_device_with_vid_pid(NULL, 0x16C0, 0x05DC);
  if (!device_handle)
  {
    fprintf(stderr, "Error finding USB device\n");
    return -1;
  }

#if 1
  int rc;
  rc = libusb_claim_interface(device_handle, 0);
  if (rc < 0)
  {
    fprintf(stderr, "Error claiming interface: %s\n", libusb_error_name(rc));
    return -1;
  }
  interface_claimed = 1;
#endif
  return 0;
}

int send_one_packet()
{
  uint8_t buf[32];
  buf[0] = 0xAB; // 1010 1011
  buf[1] = 0x00;
  buf[2] = 0x00;
  buf[3] = 0x00;
  for(int i = 4; i < 32; i++)
    buf[i] = 0x00;
  uint16_t datalen = 32;
  uint8_t data1 = 0; // currently no use
  uint8_t bRequest = 0; // currently no use
  uint16_t wIndex = 0; // currently no use
  uint16_t wValue = 0; // wValue: 0-no continuation, 1-continuation
  uint16_t timeout_ms = 100; // 10 ms waiting for response

  int  response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_OUT|LIBUSB_REQUEST_TYPE_VENDOR|data1),
      bRequest, wValue, wIndex, buf, datalen, timeout_ms);

#if 1
  //usleep(1000000);

  response = libusb_control_transfer(device_handle, (uint8_t)(LIBUSB_ENDPOINT_IN|LIBUSB_REQUEST_TYPE_VENDOR|data1),
      bRequest, wValue, wIndex, buf, datalen, timeout_ms);
  
  print_hex_buf(buf, datalen);
#endif
  return response;
}


int test_read(size_t addr, size_t len)
{
  uint8_t *buf; // buffer 64K
  buf = (uint8_t *)malloc(len * sizeof(uint8_t));
  flash_read(buf, addr, len); // read complete buffer from flash address 0

  // print start of the buffer
  printf("address 0x%06X length %d\n", addr, len);
  print_hex_buf(buf, len);
  free(buf);
  return 0;
}

int main(void)
{
  // vendorspecific();

  if(open_usb_device(0x16C0, 0x05DC) < 0)
    return -1;

  //send_one_packet();
  //send_one_packet();
  
  uint8_t flash_id;
  for(int i = 0; i < 3; i++)
    flash_id = flash_read_id(0xAB);
  printf("FLASH ID: 0x%02X\n", flash_id);
  
  uint8_t flash_status = flash_read_status(0x05);
  printf("FLASH STATUS: 0x%02X\n", flash_status);
  
  //usleep(1000000);
  // test_read(0x300000); // alphabet
  test_read(0x200000+2*1024-64, 128); // alphabet
  
  //flash_erase_sector(0x200000, 4*1024);
  size_t length = 4096;
  uint8_t *data = (uint8_t *)malloc(length * sizeof(uint8_t));
  for(int i = 0; i < length; i++)
    data[i] = 0xFF & i;
  // flash_write(data, 0x200000+2*1024, length);
  free(data);
  test_read(0x200000+2*1024-64, 256); // alphabet
  // read_flash_write_file("/tmp/flashcontent.bin", 0, 0x400000);
  // read_file_write_flash("/tmp/flashcontent.bin", 0, 16000);
  read_file_write_flash("/tmp/flashcontent.bin", 0x280000, 128*1024);
  // read_file_write_flash("-", 5155, 90016000);
  // read_file_write_flash("/tmp/alphabet.bin", 0x200000+2*1024-64, 5000);

  return 0;
}
