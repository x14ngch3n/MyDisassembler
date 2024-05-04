#pragma once
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "constants.h"
#include "modrm.h"
#include "table.h"

struct State {
    const std::vector<unsigned char>& objectSource;

    bool hasREX;
    // bool hasModRM;
    bool hasSIB;
    bool hasDisp8;
    bool hasDisp32;

    size_t curIdx;
    size_t instructionLen;
    size_t prefixOffset;

    int prefixInstructionByte, opcodeByte, modrmByte, sibByte;

    Mnemonic mnemonic;
    Prefix prefix;
    REX rex;
    ModRM modrm;
    SIB sib;

    OpEnc opEnc;
    std::vector<std::string> remOps;
    std::vector<Operand> operands;

    std::string disp8;
    std::string disp32;

    std::vector<std::string> assemblyInstruction;
    std::vector<std::string> assemblyOperands;

    State(const std::vector<unsigned char>& objectSource)
        : objectSource(objectSource),
          hasREX(false),
          hasSIB(false),
          hasDisp8(false),
          hasDisp32(false),
          curIdx(0),
          instructionLen(0),
          prefixOffset(0),
          prefix(Prefix::NONE) {}

    void init() {
        hasREX = hasSIB = hasDisp8 = hasDisp32 = false;
        curIdx = 0;
        instructionLen = 0;
        prefixOffset = 0;
        prefix = Prefix::NONE;
        // disp8 = 0;

        prefixInstructionByte = opcodeByte = modrmByte = sibByte = -1;

        remOps.clear();
        remOps.shrink_to_fit();
        operands.clear();
        operands.shrink_to_fit();

        assemblyInstruction.clear();
        assemblyInstruction.shrink_to_fit();
        assemblyOperands.clear();
        assemblyOperands.shrink_to_fit();
    }

    void parsePrefix() {
        if (objectSource[curIdx] == 0x66) {
            prefix = Prefix::P66;
            instructionLen += 1;
            curIdx += 1;
        }
    }

    void parsePrefixInstructions() {
        int startByte = objectSource[curIdx];
        if (PREFIX_INSTRUCTIONS_BYTES_SET.find(startByte) !=
            PREFIX_INSTRUCTIONS_BYTES_SET.end()) {
            // eat prefix
            prefixInstructionByte = startByte;
            prefixOffset = 1;
            instructionLen += 1;
            curIdx += 1;
        }
    }

    void parseREX() {
        // The format of REX prefix is 0100|W|R|X|B
        if ((objectSource[curIdx] >> 4) == 4) {
            hasREX = true;
            rex = REX(objectSource[curIdx]);
            instructionLen += 1;
            curIdx += 1;

            if (rex.rexW) {
                prefix = Prefix::REXW;
            } else {
                prefix = Prefix::REX;
            }
        }
    }

    void parseOpecode() {
        // eat opecode
        opcodeByte = objectSource[curIdx];
        instructionLen += 1;
        curIdx += 1;

        if (TWO_BYTES_OPCODE_PREFIX.find(opcodeByte) !=
            TWO_BYTES_OPCODE_PREFIX.end()) {
            opcodeByte = (opcodeByte << 8) + objectSource[curIdx];
            instructionLen += 1;
            curIdx += 1;
        }

        // (prefix, opcode) -> (reg, mnemonic)
        std::unordered_map<int, Mnemonic> reg2mnem;
        if (OP_LOOKUP.find(std::make_pair(prefix, opcodeByte)) !=
            OP_LOOKUP.end()) {
            reg2mnem = OP_LOOKUP.at(std::make_pair(prefix, opcodeByte));
        } else if (prefix == Prefix::REXW &&
                   OP_LOOKUP.find(std::make_pair(Prefix::REX, opcodeByte)) !=
                       OP_LOOKUP.end()) {
            reg2mnem = OP_LOOKUP.at(std::make_pair(Prefix::REX, opcodeByte));
            prefix = Prefix::REX;
        } else if (prefix == Prefix::REX &&
                   OP_LOOKUP.find(std::make_pair(Prefix::NONE, opcodeByte)) !=
                       OP_LOOKUP.end()) {
            reg2mnem = OP_LOOKUP.at(std::make_pair(Prefix::NONE, opcodeByte));
            prefix = Prefix::NONE;
        } else {
            throw std::runtime_error(
                "Unknown combination of the prefix and the opcodeByte: (" +
                to_string(prefix) + ", " + std::to_string(opcodeByte) + ")");
        }

        // We sometimes need reg of modrm to determine the opecode
        // e.g. 83 /4 -> AND
        //      83 /1 -> OR
        if (curIdx < objectSource.size()) {
            modrmByte = objectSource[curIdx];
        }

        if (modrmByte >= 0) {
            int reg = getRegVal(modrmByte);
            mnemonic = (reg2mnem.find(reg) != reg2mnem.end()) ? reg2mnem[reg]
                                                              : reg2mnem[-1];
        } else {
            mnemonic = reg2mnem[-1];
        }

        assemblyInstruction.push_back(to_string(mnemonic));
        if (OPERAND_LOOKUP.find(std::make_tuple(
                prefix, mnemonic, opcodeByte)) != OPERAND_LOOKUP.end()) {
            std::tuple<OpEnc, std::vector<std::string>, std::vector<Operand>>
                res = OPERAND_LOOKUP.at(
                    std::make_tuple(prefix, mnemonic, opcodeByte));
            opEnc = std::get<0>(res);
            remOps = std::get<1>(res);
            operands = std::get<2>(res);
        } else {
            throw std::runtime_error(
                "Unknown combination of prefix, mnemonic and opcodeByte: (" +
                to_string(prefix) + ", " + to_string(mnemonic) + ", " +
                std::to_string(opcodeByte) + ")");
        }
    }

    void parseModRM() {
        if (hasModrm(opEnc)) {
            if (modrmByte < 0) {
                throw std::runtime_error(
                    "Expected ModRM byte but there aren't any bytes left.");
            }
            instructionLen += 1;
            curIdx += 1;
            modrm = ModRM(modrmByte, rex);
        }
    }

    void parseSIB() {
        if (hasModrm(opEnc) && modrm.hasSib) {
            // eat the sib (1 byte)
            if (curIdx < objectSource.size()) {
                sibByte = objectSource[curIdx];
            }
            if (sibByte < 0) {
                throw std::runtime_error(
                    "Expected SIB byte but there aren't any bytes left.");
            }
            sib = SIB(sibByte, modrm.modByte, rex);
            instructionLen += 1;
            curIdx += 1;
        }
    }

    void parseAddressOffset() {
        if ((hasModrm(opEnc) && modrm.hasDisp8) ||
            (hasModrm(opEnc) && modrm.hasSib && sib.hasDisp8) ||
            (hasModrm(opEnc) && modrm.hasSib && modrm.modByte == 1 &&
             sib.baseByte == 5)) {
            disp8 = std::to_string(objectSource[curIdx]);
            hasDisp8 = true;
            instructionLen += 1;
            curIdx += 1;
        }

        if ((hasModrm(opEnc) && modrm.hasDisp32) ||
            (hasModrm(opEnc) && modrm.hasSib && sib.hasDisp32) ||
            (hasModrm(opEnc) && modrm.hasSib &&
             (modrm.modByte == 0 || modrm.modByte == 2) && sib.baseByte == 5)) {
            std::vector<uint8_t> _disp32 =
                std::vector<uint8_t>(objectSource.begin() + curIdx,
                                     objectSource.begin() + curIdx + 4);
            std::reverse(_disp32.begin(), _disp32.end());
            std::stringstream ss;
            ss << "0x";
            for (unsigned char x : _disp32) {
                ss << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<int>(x);
            }
            disp32 = ss.str();

            hasDisp32 = true;
            instructionLen += 4;
            curIdx += 4;
        }
    }

    // startIdx, targetLen, mnemonic, assemblyStr
    std::tuple<int, int, std::string, std::string> decodeSingleInstruction(
        int startIdx) {
        // ############### Initialize ##############################
        init();
        curIdx = startIdx;

        // the general format of the x86-64 operations
        // |prefix|REX prefix|opecode|ModR/M|SIB|address offset|immediate|

        parsePrefixInstructions();
        parsePrefix();
        parseREX();
        parseOpecode();
        parseModRM();
        parseSIB();
        parseAddressOffset();

        // ############### Process Operands ################
        std::vector<uint8_t> imm;
        for (Operand& operand : operands) {
            std::string decodedTranslatedValue;

            if (isA_REG(operand)) {
                decodedTranslatedValue = to_string(operand);
            } else if (isRM(operand) || isREG(operand)) {
                if (hasModrm(opEnc)) {
                    if (isRM(operand)) {
                        decodedTranslatedValue =
                            modrm.getAddrMode(operand, disp8, disp32);
                    } else {
                        decodedTranslatedValue = modrm.getReg(operand);
                    }
                } else {
                    if (is8Bit(operand)) {
                        decodedTranslatedValue =
                            REGISTERS8.at(std::stoi(remOps[0]));
                    } else if (is16Bit(operand)) {
                        decodedTranslatedValue =
                            REGISTERS16.at(std::stoi(remOps[0]));
                    } else if (is32Bit(operand)) {
                        decodedTranslatedValue =
                            REGISTERS32.at(std::stoi(remOps[0]));
                    } else if (is64Bit(operand)) {
                        decodedTranslatedValue =
                            REGISTERS64.at(std::stoi(remOps[0]));
                    }
                }

                if (isRM(operand) && hasModrm(opEnc) && modrm.hasSib) {
                    decodedTranslatedValue =
                        sib.getAddr(operand, disp8, disp32);
                }
            } else if (isIMM(operand)) {
                int immSize = 0;
                if (operand == Operand::imm64) {
                    immSize = 8;
                } else if (operand == Operand::imm32) {
                    immSize = 4;
                } else if (operand == Operand::imm16) {
                    immSize = 2;
                } else if (operand == Operand::imm8) {
                    immSize = 1;
                }
                imm = std::vector<uint8_t>(
                    objectSource.begin() + curIdx,
                    objectSource.begin() + curIdx + immSize);
                std::reverse(imm.begin(), imm.end());
                instructionLen += immSize;
                curIdx += immSize;

                std::stringstream ss;
                ss << "0x";
                for (unsigned char x : imm) {
                    ss << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<int>(x);
                }
                decodedTranslatedValue = ss.str();
            }

            assemblyOperands.push_back(decodedTranslatedValue);
        }

        std::string ao = "";
        for (std::string& a : assemblyOperands) {
            ao += " " + a;
        }
        assemblyInstruction.push_back(ao);

        std::string assemblyInstructionStr = "";
        for (std::string& a : assemblyInstruction) {
            assemblyInstructionStr += " " + a;
        }

        std::cout << startIdx << " " << instructionLen << " "
                  << assemblyInstructionStr << std::endl;

        return std::make_tuple(startIdx, instructionLen, to_string(mnemonic),
                               assemblyInstructionStr);
    }
};
