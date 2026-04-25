#include <iostream>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

using namespace std;

const uint32_t MEM_SIZE = 0x20000;  // 128KB memory
const uint32_t UART_ADDR = 0x10000000;  // UART output address

class RISCVSimulator {
private:
    uint32_t reg[32];
    uint8_t memory[MEM_SIZE];
    uint32_t pc;
    bool running;
    
    // Decode instruction fields
    uint32_t get_opcode(uint32_t inst) { return inst & 0x7F; }
    uint32_t get_rd(uint32_t inst) { return (inst >> 7) & 0x1F; }
    uint32_t get_funct3(uint32_t inst) { return (inst >> 12) & 0x7; }
    uint32_t get_rs1(uint32_t inst) { return (inst >> 15) & 0x1F; }
    uint32_t get_rs2(uint32_t inst) { return (inst >> 20) & 0x1F; }
    uint32_t get_funct7(uint32_t inst) { return (inst >> 25) & 0x7F; }
    
    // Immediate extraction
    int32_t get_imm_I(uint32_t inst) {
        return (int32_t)inst >> 20;
    }
    
    int32_t get_imm_S(uint32_t inst) {
        uint32_t imm = ((inst >> 7) & 0x1F) | ((inst >> 20) & 0xFE0);
        return (imm & 0x800) ? (int32_t)(imm | 0xFFFFF000) : (int32_t)imm;
    }
    
    int32_t get_imm_B(uint32_t inst) {
        uint32_t imm = ((inst >> 7) & 0x1E) | ((inst >> 20) & 0x7E0) | 
                       ((inst << 4) & 0x800) | ((inst >> 19) & 0x1000);
        return (imm & 0x1000) ? (int32_t)(imm | 0xFFFFE000) : (int32_t)imm;
    }
    
    int32_t get_imm_U(uint32_t inst) {
        return (int32_t)(inst & 0xFFFFF000);
    }
    
    int32_t get_imm_J(uint32_t inst) {
        uint32_t imm = ((inst >> 20) & 0x7FE) | ((inst >> 9) & 0x800) |
                       (inst & 0xFF000) | ((inst >> 11) & 0x100000);
        return (imm & 0x100000) ? (int32_t)(imm | 0xFFE00000) : (int32_t)imm;
    }
    
    uint32_t read_word(uint32_t addr) {
        if (addr + 3 >= MEM_SIZE) return 0;
        return *(uint32_t*)&memory[addr];
    }
    
    uint16_t read_halfword(uint32_t addr) {
        if (addr + 1 >= MEM_SIZE) return 0;
        return *(uint16_t*)&memory[addr];
    }
    
    uint8_t read_byte(uint32_t addr) {
        if (addr >= MEM_SIZE) return 0;
        return memory[addr];
    }
    
    void write_word(uint32_t addr, uint32_t value) {
        if (addr == UART_ADDR) {
            cout << (char)(value & 0xFF);
            return;
        }
        if (addr + 3 >= MEM_SIZE) return;
        *(uint32_t*)&memory[addr] = value;
    }
    
    void write_halfword(uint32_t addr, uint16_t value) {
        if (addr == UART_ADDR) {
            cout << (char)(value & 0xFF);
            return;
        }
        if (addr + 1 >= MEM_SIZE) return;
        *(uint16_t*)&memory[addr] = value;
    }
    
    void write_byte(uint32_t addr, uint8_t value) {
        if (addr == UART_ADDR) {
            // Memory-mapped I/O for UART output
            cout << (char)value;
            return;
        }
        if (addr >= MEM_SIZE) return;
        memory[addr] = value;
    }
    
public:
    RISCVSimulator() {
        memset(reg, 0, sizeof(reg));
        memset(memory, 0, sizeof(memory));
        pc = 0;
        running = true;
        // Initialize stack pointer to top of memory
        reg[2] = MEM_SIZE - 4;  // sp = x2
    }
    
    void load_program(const vector<uint8_t>& data) {
        size_t size = min(data.size(), (size_t)MEM_SIZE);
        memcpy(memory, data.data(), size);
    }
    
    void execute() {
        while (running) {
            uint32_t inst = read_word(pc);
            uint32_t opcode = get_opcode(inst);
            
            reg[0] = 0;  // x0 is always 0
            
            switch (opcode) {
                case 0x37: { // LUI
                    uint32_t rd = get_rd(inst);
                    int32_t imm = get_imm_U(inst);
                    reg[rd] = imm;
                    pc += 4;
                    break;
                }
                case 0x17: { // AUIPC
                    uint32_t rd = get_rd(inst);
                    int32_t imm = get_imm_U(inst);
                    reg[rd] = pc + imm;
                    pc += 4;
                    break;
                }
                case 0x6F: { // JAL
                    uint32_t rd = get_rd(inst);
                    int32_t imm = get_imm_J(inst);
                    reg[rd] = pc + 4;
                    pc += imm;
                    break;
                }
                case 0x67: { // JALR
                    uint32_t rd = get_rd(inst);
                    uint32_t rs1 = get_rs1(inst);
                    int32_t imm = get_imm_I(inst);
                    uint32_t new_pc = (reg[rs1] + imm) & ~1;
                    reg[rd] = pc + 4;
                    pc = new_pc;
                    break;
                }
                case 0x63: { // Branch instructions
                    uint32_t funct3 = get_funct3(inst);
                    uint32_t rs1 = get_rs1(inst);
                    uint32_t rs2 = get_rs2(inst);
                    int32_t imm = get_imm_B(inst);
                    bool branch = false;
                    
                    switch (funct3) {
                        case 0x0: branch = (reg[rs1] == reg[rs2]); break; // BEQ
                        case 0x1: branch = (reg[rs1] != reg[rs2]); break; // BNE
                        case 0x4: branch = ((int32_t)reg[rs1] < (int32_t)reg[rs2]); break; // BLT
                        case 0x5: branch = ((int32_t)reg[rs1] >= (int32_t)reg[rs2]); break; // BGE
                        case 0x6: branch = (reg[rs1] < reg[rs2]); break; // BLTU
                        case 0x7: branch = (reg[rs1] >= reg[rs2]); break; // BGEU
                    }
                    
                    pc = branch ? pc + imm : pc + 4;
                    break;
                }
                case 0x03: { // Load instructions
                    uint32_t funct3 = get_funct3(inst);
                    uint32_t rd = get_rd(inst);
                    uint32_t rs1 = get_rs1(inst);
                    int32_t imm = get_imm_I(inst);
                    uint32_t addr = reg[rs1] + imm;
                    
                    switch (funct3) {
                        case 0x0: reg[rd] = (int32_t)(int8_t)read_byte(addr); break; // LB
                        case 0x1: reg[rd] = (int32_t)(int16_t)read_halfword(addr); break; // LH
                        case 0x2: reg[rd] = read_word(addr); break; // LW
                        case 0x4: reg[rd] = read_byte(addr); break; // LBU
                        case 0x5: reg[rd] = read_halfword(addr); break; // LHU
                    }
                    pc += 4;
                    break;
                }
                case 0x23: { // Store instructions
                    uint32_t funct3 = get_funct3(inst);
                    uint32_t rs1 = get_rs1(inst);
                    uint32_t rs2 = get_rs2(inst);
                    int32_t imm = get_imm_S(inst);
                    uint32_t addr = reg[rs1] + imm;
                    
                    switch (funct3) {
                        case 0x0: write_byte(addr, reg[rs2]); break; // SB
                        case 0x1: write_halfword(addr, reg[rs2]); break; // SH
                        case 0x2: write_word(addr, reg[rs2]); break; // SW
                    }
                    pc += 4;
                    break;
                }
                case 0x13: { // Immediate arithmetic
                    uint32_t funct3 = get_funct3(inst);
                    uint32_t rd = get_rd(inst);
                    uint32_t rs1 = get_rs1(inst);
                    int32_t imm = get_imm_I(inst);
                    
                    switch (funct3) {
                        case 0x0: reg[rd] = reg[rs1] + imm; break; // ADDI
                        case 0x2: reg[rd] = ((int32_t)reg[rs1] < imm) ? 1 : 0; break; // SLTI
                        case 0x3: reg[rd] = (reg[rs1] < (uint32_t)imm) ? 1 : 0; break; // SLTIU
                        case 0x4: reg[rd] = reg[rs1] ^ imm; break; // XORI
                        case 0x6: reg[rd] = reg[rs1] | imm; break; // ORI
                        case 0x7: reg[rd] = reg[rs1] & imm; break; // ANDI
                        case 0x1: reg[rd] = reg[rs1] << (imm & 0x1F); break; // SLLI
                        case 0x5: {
                            if ((imm & 0x400) == 0)
                                reg[rd] = reg[rs1] >> (imm & 0x1F); // SRLI
                            else
                                reg[rd] = (int32_t)reg[rs1] >> (imm & 0x1F); // SRAI
                            break;
                        }
                    }
                    pc += 4;
                    break;
                }
                case 0x33: { // Register arithmetic
                    uint32_t funct3 = get_funct3(inst);
                    uint32_t funct7 = get_funct7(inst);
                    uint32_t rd = get_rd(inst);
                    uint32_t rs1 = get_rs1(inst);
                    uint32_t rs2 = get_rs2(inst);
                    
                    if (funct7 == 0x01) { // RV32M extension
                        switch (funct3) {
                            case 0x0: // MUL
                                reg[rd] = (int32_t)reg[rs1] * (int32_t)reg[rs2];
                                break;
                            case 0x1: { // MULH
                                int64_t result = (int64_t)(int32_t)reg[rs1] * (int64_t)(int32_t)reg[rs2];
                                reg[rd] = (uint32_t)(result >> 32);
                                break;
                            }
                            case 0x2: { // MULHSU
                                int64_t result = (int64_t)(int32_t)reg[rs1] * (uint64_t)reg[rs2];
                                reg[rd] = (uint32_t)(result >> 32);
                                break;
                            }
                            case 0x3: { // MULHU
                                uint64_t result = (uint64_t)reg[rs1] * (uint64_t)reg[rs2];
                                reg[rd] = (uint32_t)(result >> 32);
                                break;
                            }
                            case 0x4: // DIV
                                if (reg[rs2] == 0)
                                    reg[rd] = 0xFFFFFFFF;
                                else
                                    reg[rd] = (int32_t)reg[rs1] / (int32_t)reg[rs2];
                                break;
                            case 0x5: // DIVU
                                if (reg[rs2] == 0)
                                    reg[rd] = 0xFFFFFFFF;
                                else
                                    reg[rd] = reg[rs1] / reg[rs2];
                                break;
                            case 0x6: // REM
                                if (reg[rs2] == 0)
                                    reg[rd] = reg[rs1];
                                else
                                    reg[rd] = (int32_t)reg[rs1] % (int32_t)reg[rs2];
                                break;
                            case 0x7: // REMU
                                if (reg[rs2] == 0)
                                    reg[rd] = reg[rs1];
                                else
                                    reg[rd] = reg[rs1] % reg[rs2];
                                break;
                        }
                    } else {
                        switch (funct3) {
                            case 0x0:
                                if (funct7 == 0x00)
                                    reg[rd] = reg[rs1] + reg[rs2]; // ADD
                                else
                                    reg[rd] = reg[rs1] - reg[rs2]; // SUB
                                break;
                            case 0x1: reg[rd] = reg[rs1] << (reg[rs2] & 0x1F); break; // SLL
                            case 0x2: reg[rd] = ((int32_t)reg[rs1] < (int32_t)reg[rs2]) ? 1 : 0; break; // SLT
                            case 0x3: reg[rd] = (reg[rs1] < reg[rs2]) ? 1 : 0; break; // SLTU
                            case 0x4: reg[rd] = reg[rs1] ^ reg[rs2]; break; // XOR
                            case 0x5:
                                if (funct7 == 0x00)
                                    reg[rd] = reg[rs1] >> (reg[rs2] & 0x1F); // SRL
                                else
                                    reg[rd] = (int32_t)reg[rs1] >> (reg[rs2] & 0x1F); // SRA
                                break;
                            case 0x6: reg[rd] = reg[rs1] | reg[rs2]; break; // OR
                            case 0x7: reg[rd] = reg[rs1] & reg[rs2]; break; // AND
                        }
                    }
                    pc += 4;
                    break;
                }
                case 0x0F: { // FENCE
                    pc += 4;
                    break;
                }
                case 0x73: { // ECALL/EBREAK
                    uint32_t funct3 = get_funct3(inst);
                    if (funct3 == 0) {
                        uint32_t funct12 = inst >> 20;
                        if (funct12 == 0) { // ECALL
                            // Handle system calls based on a7 (x17)
                            uint32_t syscall_num = reg[17];
                            
                            if (syscall_num == 64) { // sys_write
                                uint32_t fd = reg[10];      // a0: file descriptor
                                uint32_t buf = reg[11];     // a1: buffer address
                                uint32_t count = reg[12];   // a2: count
                                
                                if (fd == 1) { // stdout
                                    for (uint32_t i = 0; i < count; i++) {
                                        cout << (char)read_byte(buf + i);
                                    }
                                }
                                reg[10] = count; // return bytes written
                            } else if (syscall_num == 93) { // sys_exit
                                running = false;
                            } else if (syscall_num == 1) { // simple putchar
                                cout << (char)(reg[10] & 0xFF);
                            } else {
                                // Unknown syscall, just continue
                            }
                        } else { // EBREAK
                            running = false;
                        }
                    }
                    pc += 4;
                    break;
                }
                default:
                    // Unknown instruction - stop execution
                    running = false;
                    break;
            }
        }
    }
    
    uint32_t get_reg(int i) { return reg[i]; }
};

int main() {
    vector<uint8_t> program_data;
    
    // Read binary data from stdin
    uint8_t byte;
    while (cin.read((char*)&byte, 1)) {
        program_data.push_back(byte);
    }
    
    RISCVSimulator sim;
    sim.load_program(program_data);
    sim.execute();
    
    return 0;
}
