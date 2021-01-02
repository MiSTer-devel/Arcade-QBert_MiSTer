
module ram
#(
  parameter addr_width=16,
  parameter data_width=8
)
(
  input clk,
  input [data_width-1:0] din,
  input [addr_width-1:0] addr,
  input cs,
  input oe,
  input wr,
  output reg [data_width-1:0] Q
);


reg [data_width-1:0] memory[(1<<addr_width)-1:0];

always @(posedge clk) begin
  Q <= 0;
  if (~cs) begin
    if (~oe) Q <= memory[addr];
    if (~wr) memory[addr] <= din;
  end
end

endmodule
