module usb_hid_ctrl_ep (
  input clk,
  input reset,
  output [6:0] dev_addr,
  
  output reg [7:0] debug_led,

  ////////////////////
  // out endpoint interface 
  ////////////////////
  output out_ep_req,
  input out_ep_grant,
  input out_ep_data_avail,
  input out_ep_setup,
  output out_ep_data_get,
  input [7:0] out_ep_data,
  output out_ep_stall,
  input out_ep_acked,


  ////////////////////
  // in endpoint interface 
  ////////////////////
  output in_ep_req,
  input in_ep_grant,
  input in_ep_data_free,
  output in_ep_data_put,
  output [7:0] in_ep_data,
  output in_ep_data_done,
  output reg in_ep_stall,
  input in_ep_acked
);
  

  localparam IDLE = 0;
  localparam SETUP = 1;
  localparam DATA_IN = 2;
  localparam DATA_OUT = 3;
  localparam STATUS_IN = 4;
  localparam STATUS_OUT = 5;
  
  reg [5:0] ctrl_xfr_state = IDLE;
  reg [5:0] ctrl_xfr_state_next;
 
 
 
  reg setup_stage_end = 0;
  reg data_stage_end = 0;
  reg status_stage_end = 0;
  reg send_zero_length_data_pkt = 0;



  // the default control endpoint gets assigned the device address
  reg [6:0] dev_addr_i = 0;
  assign dev_addr = dev_addr_i;

  assign out_ep_req = out_ep_data_avail;
  assign out_ep_data_get = out_ep_data_avail;
  reg out_ep_data_valid = 0;
  always @(posedge clk) out_ep_data_valid <= out_ep_data_avail && out_ep_grant;

  // need to record the setup data
  reg [3:0] setup_data_addr = 0;
  reg [9:0] raw_setup_data [7:0];

  wire [7:0] bmRequestType = raw_setup_data[0];
  wire [7:0] bRequest = raw_setup_data[1];
  wire [15:0] wValue = {raw_setup_data[3][7:0], raw_setup_data[2][7:0]};
  wire [15:0] wIndex = {raw_setup_data[5][7:0], raw_setup_data[4][7:0]};
  wire [15:0] wLength = {raw_setup_data[7][7:0], raw_setup_data[6][7:0]};

  // keep track of new out data start and end
  wire pkt_start;
  wire pkt_end;

  rising_edge_detector detect_pkt_start (
    .clk(clk),
    .in(out_ep_data_avail),
    .out(pkt_start)
  );

  falling_edge_detector detect_pkt_end (
    .clk(clk),
    .in(out_ep_data_avail),
    .out(pkt_end)
  );

  assign out_ep_stall = 1'b0;

  wire setup_pkt_start = pkt_start && out_ep_setup;

  wire has_data_stage = wLength != 16'b0000000000000000;

  wire out_data_stage;
  assign out_data_stage = has_data_stage && !bmRequestType[7];

  wire in_data_stage;
  assign in_data_stage = has_data_stage && bmRequestType[7];

  reg [7:0] bytes_sent = 0;
  reg [6:0] rom_length = 0;

  wire all_data_sent = 
    (bytes_sent >= rom_length) ||
    (bytes_sent >= wLength);

  wire more_data_to_send =
    !all_data_sent;

  wire in_data_transfer_done;

  rising_edge_detector detect_in_data_transfer_done (
    .clk(clk),
    .in(all_data_sent),
    .out(in_data_transfer_done)
  );

  assign in_ep_data_done = (in_data_transfer_done && ctrl_xfr_state == DATA_IN) || send_zero_length_data_pkt;

  assign in_ep_req = ctrl_xfr_state == DATA_IN && more_data_to_send;
  assign in_ep_data_put = ctrl_xfr_state == DATA_IN && more_data_to_send && in_ep_data_free;


  reg [6:0] rom_addr = 0;

  reg save_dev_addr = 0;
  reg [6:0] new_dev_addr = 0;

  ////////////////////////////////////////////////////////////////////////////////
  // control transfer state machine
  ////////////////////////////////////////////////////////////////////////////////


  always @* begin
    setup_stage_end <= 0;
    data_stage_end <= 0;
    status_stage_end <= 0;
    send_zero_length_data_pkt <= 0;

    case (ctrl_xfr_state)
      IDLE : begin
        if (setup_pkt_start) begin
          ctrl_xfr_state_next <= SETUP;
        end else begin
          ctrl_xfr_state_next <= IDLE;
        end
      end

      SETUP : begin
        if (pkt_end) begin
          setup_stage_end <= 1;

          if (in_data_stage) begin
            ctrl_xfr_state_next <= DATA_IN;

          end else if (out_data_stage) begin
            ctrl_xfr_state_next <= DATA_OUT;

          end else begin
            ctrl_xfr_state_next <= STATUS_IN;
            send_zero_length_data_pkt <= 1;
          end

        end else begin
          ctrl_xfr_state_next <= SETUP;
        end
      end

      DATA_IN : begin
	if (in_ep_stall) begin
          ctrl_xfr_state_next <= IDLE;
          data_stage_end <= 1;
          status_stage_end <= 1;

	end else if (in_ep_acked && all_data_sent) begin
          ctrl_xfr_state_next <= STATUS_OUT;
          data_stage_end <= 1;

        end else begin
          ctrl_xfr_state_next <= DATA_IN;
        end
      end

      DATA_OUT : begin
        if (out_ep_acked) begin
          ctrl_xfr_state_next <= STATUS_IN;
          send_zero_length_data_pkt <= 1;
          data_stage_end <= 1;
          
        end else begin
          ctrl_xfr_state_next <= DATA_OUT;
        end
      end

      STATUS_IN : begin
        if (in_ep_acked) begin
          ctrl_xfr_state_next <= IDLE;
          status_stage_end <= 1;
          
        end else begin
          ctrl_xfr_state_next <= STATUS_IN;
        end
      end

      STATUS_OUT: begin
        if (out_ep_acked) begin
          ctrl_xfr_state_next <= IDLE;
          status_stage_end <= 1;
          
        end else begin
          ctrl_xfr_state_next <= STATUS_OUT;
        end
      end

      default begin
        ctrl_xfr_state_next <= IDLE;
      end
    endcase
  end

  always @(posedge clk) begin
    if (reset) begin
      ctrl_xfr_state <= IDLE;
    end else begin
      ctrl_xfr_state <= ctrl_xfr_state_next;
    end
  end

  always @(posedge clk) begin
    in_ep_stall <= 0;

    if (out_ep_setup && out_ep_data_valid) begin
      raw_setup_data[setup_data_addr] <= out_ep_data;
      setup_data_addr <= setup_data_addr + 1;
    end

    if (setup_stage_end) begin
    case (bmRequestType[6:5]) // 2 bits describing request type
      0 : begin // 0: standard request
      case (bRequest)
        'h06 : begin
          // GET_DESCRIPTOR
          case (wValue[15:8]) 
            1 : begin
              // DEVICE
              rom_addr    <= 0; 
              rom_length  <= 18;
              debug_led <= 8'b11000011;
            end 

            2 : begin
              // CONFIGURATION
              rom_addr    <= 18; 
              rom_length  <= 67;
            end 
            
            6 : begin
              // DEVICE_QUALIFIER
              in_ep_stall <= 1;
              rom_addr   <= 0;
              rom_length <= 0;
            end
            
          endcase
        end

        'h05 : begin
          // SET_ADDRESS
          rom_addr   <= 0;
          rom_length <= 0;

          // we need to save the address after the status stage ends
          // this is because the status stage token will still be using
          // the old device address
          save_dev_addr <= 1;
          new_dev_addr <= wValue[6:0]; 
        end

        'h09 : begin
          // SET_CONFIGURATION
          rom_addr   <= 0;
          rom_length <= 0;
        end

        'h20 : begin
          // SET_LINE_CODING
          rom_addr   <= 0;
          rom_length <= 0;
        end

        'h21 : begin
          // GET_LINE_CODING
          rom_addr   <= 85;
          rom_length <= 7;
        end

        'h22 : begin
          // SET_CONTROL_LINE_STATE
          rom_addr   <= 0;
          rom_length <= 0;
        end

        'h23 : begin
          // SEND_BREAK
          rom_addr   <= 0;
          rom_length <= 0;
        end

        default begin
          rom_addr   <= 0;
          rom_length <= 0;
        end
      endcase
      end // end 0: standard request
      default begin // 2: vendor specific request (also would handle 1 or 3)
        debug_led <= wValue[7:0];
      end // end 2: vendor specific request
    endcase
    end

    if (ctrl_xfr_state == DATA_IN && more_data_to_send && in_ep_grant && in_ep_data_free) begin
      rom_addr <= rom_addr + 1;
      bytes_sent <= bytes_sent + 1;
    end

    if (status_stage_end) begin
      setup_data_addr <= 0;      
      bytes_sent <= 0;
      rom_addr <= 0;
      rom_length <= 0;

      if (save_dev_addr) begin
        save_dev_addr <= 0;
        dev_addr_i <= new_dev_addr; 
      end 
    end

    if (reset) begin
      bytes_sent <= 0;
      rom_addr <= 0;
      rom_length <= 0;
      dev_addr_i <= 0;
      setup_data_addr <= 0;
      save_dev_addr <= 0;
    end
  end


  `define CDC_ACM_ENDPOINT 2 
  `define CDC_RX_ENDPOINT 1
  `define CDC_TX_ENDPOINT 1
	

  wire [7:0] descriptor_rom [0:91];
  assign in_ep_data = descriptor_rom[rom_addr];

    assign descriptor_rom[0] = 18; // bLength
      assign descriptor_rom[1] = 1; // bDescriptorType
      assign descriptor_rom[2] = 'h00; // bcdUSB[0]
      assign descriptor_rom[3] = 'h02; // bcdUSB[1]
      assign descriptor_rom[4] = 'hFF; // bDeviceClass (Communications Device Class)
      assign descriptor_rom[5] = 'h00; // bDeviceSubClass (Abstract Control Model)
      assign descriptor_rom[6] = 'h00; // bDeviceProtocol (No class specific protocol required)
      assign descriptor_rom[7] = 32; // bMaxPacketSize0

      assign descriptor_rom[8] = 'hc0; // idVendor[0] VOTI
      assign descriptor_rom[9] = 'h16; // idVendor[1]
      assign descriptor_rom[10] = 'hdc; // idProduct[0]
      assign descriptor_rom[11] = 'h05; // idProduct[1]
      
      assign descriptor_rom[12] = 0; // bcdDevice[0]
      assign descriptor_rom[13] = 0; // bcdDevice[1]
      assign descriptor_rom[14] = 0; // iManufacturer
      assign descriptor_rom[15] = 0; // iProduct
      assign descriptor_rom[16] = 0; // iSerialNumber
      assign descriptor_rom[17] = 1; // bNumConfigurations

      // configuration descriptor
      assign descriptor_rom[18] = 9; // bLength
      assign descriptor_rom[19] = 2; // bDescriptorType
      assign descriptor_rom[20] = (9+9+5+5+4+5+7+9+7+7); // wTotalLength[0] 
      assign descriptor_rom[21] = 0; // wTotalLength[1]
      assign descriptor_rom[22] = 2; // bNumInterfaces
      assign descriptor_rom[23] = 1; // bConfigurationValue
      assign descriptor_rom[24] = 0; // iConfiguration
      assign descriptor_rom[25] = 'hC0; // bmAttributes
      assign descriptor_rom[26] = 60; // bMaxPower
      
      // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
      assign descriptor_rom[27] = 9; // bLength
      assign descriptor_rom[28] = 4; // bDescriptorType
      assign descriptor_rom[29] = 0; // bInterfaceNumber
      assign descriptor_rom[30] = 0; // bAlternateSetting
      assign descriptor_rom[31] = 1; // bNumEndpoints
      assign descriptor_rom[32] = 2; // bInterfaceClass (Communications Device Class)
      assign descriptor_rom[33] = 2; // bInterfaceSubClass (Abstract Control Model)
      assign descriptor_rom[34] = 1; // bInterfaceProtocol (AT Commands: V.250 etc)
      assign descriptor_rom[35] = 0; // iInterface

      // CDC Header Functional Descriptor, CDC Spec 5.2.3.1, Table 26
      assign descriptor_rom[36] = 5; // bFunctionLength
	    assign descriptor_rom[37] = 'h24; // bDescriptorType
	    assign descriptor_rom[38] = 'h00; // bDescriptorSubtype
	    assign descriptor_rom[39] = 'h10; 
      assign descriptor_rom[40] = 'h01;	// bcdCDC

	    // Call Management Functional Descriptor, CDC Spec 5.2.3.2, Table 27
	    assign descriptor_rom[41] = 5;					// bFunctionLength
	    assign descriptor_rom[42] = 'h24;					// bDescriptorType
	    assign descriptor_rom[43] = 'h01;					// bDescriptorSubtype
	    assign descriptor_rom[44] = 'h00;					// bmCapabilities
	    assign descriptor_rom[45] = 1;					// bDataInterface

	    // Abstract Control Management Functional Descriptor, CDC Spec 5.2.3.3, Table 28
	    assign descriptor_rom[46] = 4;					// bFunctionLength
	    assign descriptor_rom[47] = 'h24;					// bDescriptorType
	    assign descriptor_rom[48] = 'h02;					// bDescriptorSubtype
	    assign descriptor_rom[49] = 'h06;					// bmCapabilities

	    // Union Functional Descriptor, CDC Spec 5.2.3.8, Table 33
    	assign descriptor_rom[50] = 5;					// bFunctionLength
    	assign descriptor_rom[51] = 'h24;					// bDescriptorType
    	assign descriptor_rom[52] = 'h06;					// bDescriptorSubtype
    	assign descriptor_rom[53] = 0;					// bMasterInterface
    	assign descriptor_rom[54] = 1;					// bSlaveInterface0

    	// endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
    	assign descriptor_rom[55] = 7; // bLength
    	assign descriptor_rom[56] = 5; // bDescriptorType
    	assign descriptor_rom[57] = `CDC_ACM_ENDPOINT | 'h80; // bEndpointAddress
    	assign descriptor_rom[58] = 'h03; // bmAttributes (0x03=intr)
    	assign descriptor_rom[59] = 8; // wMaxPacketSize[0]
      assign descriptor_rom[60] = 0; // wMaxPacketSize[1]
    	assign descriptor_rom[61] = 64; // bInterval

    	// interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
    	assign descriptor_rom[62] = 9; // bLength
    	assign descriptor_rom[63] = 4; // bDescriptorType
    	assign descriptor_rom[64] = 1; // bInterfaceNumber
    	assign descriptor_rom[65] = 0; // bAlternateSetting
    	assign descriptor_rom[66] = 2; // bNumEndpoints
    	assign descriptor_rom[67] = 'h0A; // bInterfaceClass
    	assign descriptor_rom[68] = 'h00; // bInterfaceSubClass
    	assign descriptor_rom[69] = 'h00; // bInterfaceProtocol
    	assign descriptor_rom[70] = 0; // iInterface

    	// endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
    	assign descriptor_rom[71] = 7; // bLength
    	assign descriptor_rom[72] = 5; // bDescriptorType
    	assign descriptor_rom[73] = `CDC_RX_ENDPOINT; // bEndpointAddress
    	assign descriptor_rom[74] = 'h02; // bmAttributes (0x02=bulk)
    	assign descriptor_rom[75] = 32; // wMaxPacketSize[0]
      assign descriptor_rom[76] = 0; // wMaxPacketSize[1]
    	assign descriptor_rom[77] = 0; // bInterval

    	// endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
    	assign descriptor_rom[78] = 7; // bLength
    	assign descriptor_rom[79] = 5; // bDescriptorType
    	assign descriptor_rom[80] = `CDC_TX_ENDPOINT | 'h80; // bEndpointAddress
    	assign descriptor_rom[81] = 'h02; // bmAttributes (0x02=bulk)

    	assign descriptor_rom[82] = 32; // wMaxPacketSize[0]
      assign descriptor_rom[83] = 0; // wMaxPacketSize[1]
    	assign descriptor_rom[84] = 0; // bInterval

      // LINE_CODING
      assign descriptor_rom[85] = 'h80; // dwDTERate[0]
      assign descriptor_rom[86] = 'h25; // dwDTERate[1]
      assign descriptor_rom[87] = 'h00; // dwDTERate[2]
      assign descriptor_rom[88] = 'h00; // dwDTERate[3]
      assign descriptor_rom[89] = 1; // bCharFormat (1 stop bit)
      assign descriptor_rom[90] = 0; // bParityType (None)
      assign descriptor_rom[91] = 8; // bDataBits (8 bits)

endmodule