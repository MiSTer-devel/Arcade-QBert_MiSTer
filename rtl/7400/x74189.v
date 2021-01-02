
module x74189(

  input clk,
  input [3:0] din,
  input [3:0] addr,
  input cs,
  input wr,
  output reg [3:0] Q

);

reg [3:0] memory[15:0];

always @(posedge clk) begin
  Q <= 4'd0;
  if (~cs) begin
    if (~wr) memory[addr] <= ~din;
    else Q <= memory[addr];
  end
end

endmodule
