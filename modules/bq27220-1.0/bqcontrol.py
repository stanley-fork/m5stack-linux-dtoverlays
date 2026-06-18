#!/usr/bin/env python3
"""BQ27220 control/readout utility.

Requires Linux i2c-dev and either smbus2 or python-smbus installed.
Default bus/address can be provided by env vars:
  BQ27220_BUS / BQ_BUS / I2C_BUS
  BQ27220_ADDR / BQ_ADDR / I2C_ADDR
"""

from __future__ import annotations

import argparse
import fcntl
import os
import sys
import time
from dataclasses import dataclass
from typing import Callable, Iterable, Optional

try:
    from smbus2 import SMBus, i2c_msg  # type: ignore
except ImportError:  # pragma: no cover - depends on target board
    try:
        from smbus import SMBus  # type: ignore
        i2c_msg = None  # type: ignore
    except ImportError:  # pragma: no cover
        SMBus = None  # type: ignore
        i2c_msg = None  # type: ignore


DEFAULT_ADDR = 0x55
DEFAULT_BUS = 1

# Standard commands, all little-endian word unless noted.
CMD_CONTROL = 0x00
CMD_TEMPERATURE = 0x06
CMD_VOLTAGE = 0x08
CMD_BATTERY_STATUS = 0x0A
CMD_CURRENT = 0x0C
CMD_REMAINING_CAPACITY = 0x10
CMD_FULL_CHARGE_CAPACITY = 0x12
CMD_AVERAGE_CURRENT = 0x14
CMD_TIME_TO_EMPTY = 0x16
CMD_TIME_TO_FULL = 0x18
CMD_STANDBY_CURRENT = 0x1A
CMD_RAW_COULOMB_COUNT = 0x22
CMD_AVERAGE_POWER = 0x24
CMD_INTERNAL_TEMPERATURE = 0x28
CMD_CYCLE_COUNT = 0x2A
CMD_STATE_OF_CHARGE = 0x2C
CMD_STATE_OF_HEALTH = 0x2E
CMD_CHARGING_VOLTAGE = 0x30
CMD_CHARGING_CURRENT = 0x32
CMD_OPERATION_STATUS = 0x3A
CMD_DESIGN_CAPACITY = 0x3C
CMD_MANUFACTURER_ACCESS = 0x3E
CMD_MAC_DATA = 0x40
CMD_MAC_DATA_SUM = 0x60
CMD_MAC_DATA_LEN = 0x61
CMD_RAW_CURRENT = 0x7A
CMD_RAW_VOLTAGE = 0x7C
CMD_RAW_INT_TEMP = 0x7E

I2C_SLAVE = 0x0703
I2C_SLAVE_FORCE = 0x0706

# MAC subcommands.
MAC_CONTROL_STATUS = 0x0000
MAC_DEVICE_NUMBER = 0x0001
MAC_FW_VERSION = 0x0002
MAC_BAT_INSERT = 0x000D
MAC_ENTER_CFG_UPDATE = 0x0090
MAC_EXIT_CFG_UPDATE_REINIT = 0x0091
MAC_EXIT_CFG_UPDATE = 0x0092
MAC_SET_PROFILE_1 = 0x0015

# Data memory fields used by this script. BQ27220 profile 1 addresses come
# from TRM SLUUBD4A Table 3-2. Values are written temporarily to RAM data flash.
DATA_FIELDS = {
    "Chg Inhibit Temp Low": (0x4041, "i2", "0.1C", 0),
    "Chg Inhibit Temp High": (0x4043, "i2", "0.1C", 0),
    "Chg Inhibit Temp Hys": (0x4045, "i2", "0.1C", 0),
    "Charging Current": (0x4047, "i2", "mA", 0),
    "Charging Voltage": (0x4049, "i2", "mV", 0),
    "Taper Current": (0x404D, "i2", "mA", 0),
    "Operation Config A": (0x4052, "h2", "hex", 0),
    "Operation Config B": (0x4054, "h2", "hex", 0),
    "Sleep Current": (0x4063, "i2", "mA", 0),
    "Battery Low %": (0x9251, "u2", "0.01%", 0),
    "Near Full": (0x926B, "i2", "mAh", 0),
    "CEDV Profile 1 Gauging Configuration": (0x929B, "h2", "hex", 0),
    "Full Charge Capacity": (0x929D, "i2", "mAh", 0),
    "Design Capacity": (0x929F, "i2", "mAh", 0),
    "Design Energy": (0x92A1, "i2", "mWh", 0),
    "Design Voltage": (0x92A3, "i2", "mV", 0),
    "Charge Termination Voltage": (0x92A5, "i2", "mV offset", 0),
    "EMF": (0x92A7, "u2", "raw", 0),
    "C0": (0x92A9, "u2", "raw", 0),
    "R0": (0x92AB, "u2", "raw", 0),
    "T0": (0x92AD, "u2", "raw", 0),
    "R1": (0x92AF, "u2", "raw", 0),
    "TC": (0x92B1, "u1", "raw", 0),
    "C1": (0x92B2, "u1", "raw", 0),
    "Fixed EDV 0": (0x92B4, "i2", "mV", 0),
    "Fixed EDV 1": (0x92B7, "i2", "mV", 0),
    "Fixed EDV 2": (0x92BA, "i2", "mV", 0),
}

INIT_VALUES = {
    "Full Charge Capacity": 1200,
    "Design Capacity": 1200,
    "Design Energy": 4440,
    "Design Voltage": 3700,  # User wrote 4.7 V, but 4.7 V exceeds BQ27220 normal LiPo range.
    "Chg Inhibit Temp Low": 0,      # 0.0C
    "Chg Inhibit Temp High": 550,   # 55.0C; default 45C can stop charging around warm boards.
    "Chg Inhibit Temp Hys": 50,     # 5.0C
    "Charging Current": 200,
    "Charging Voltage": 4200,
    "Taper Current": 100,
    "Fixed EDV 0": 3300,
    "Fixed EDV 1": 3400,
    "Fixed EDV 2": 3500,
    "Battery Low %": 700,
    "Near Full": 60,
}


class BQError(RuntimeError):
    pass


def parse_int_auto(value: str) -> int:
    return int(str(value), 0)


def env_int(names: Iterable[str], default: int) -> int:
    for name in names:
        value = os.getenv(name)
        if value not in (None, ""):
            return parse_int_auto(value)
    return default


def le_u16(data: bytes) -> int:
    return data[0] | (data[1] << 8)


def le_i16(data: bytes) -> int:
    value = le_u16(data)
    return value - 0x10000 if value & 0x8000 else value


def encode_value(kind: str, value: int) -> bytes:
    if kind in ("u1",):
        return bytes([value & 0xFF])
    if kind in ("i1",):
        return int(value).to_bytes(1, "big", signed=True)
    if kind in ("u2", "h2"):
        return int(value).to_bytes(2, "big", signed=False)
    if kind == "i2":
        return int(value).to_bytes(2, "big", signed=True)
    raise ValueError(f"unsupported type: {kind}")


def decode_value(kind: str, data: bytes) -> int:
    if kind in ("u1",):
        return data[0]
    if kind == "i1":
        return int.from_bytes(data[:1], "big", signed=True)
    if kind in ("u2", "h2"):
        return int.from_bytes(data[:2], "big", signed=False)
    if kind == "i2":
        return int.from_bytes(data[:2], "big", signed=True)
    raise ValueError(f"unsupported type: {kind}")


def fmt_hex(value: int, width: int = 4) -> str:
    return f"0x{value:0{width}X}"


@dataclass
class Register:
    name: str
    cmd: int
    unit: str
    signed: bool = False
    transform: Optional[Callable[[int], object]] = None


REGISTERS = [
    Register("Temperature", CMD_TEMPERATURE, "degC", False, lambda x: round(x / 10.0 - 273.15, 2)),
    Register("Voltage", CMD_VOLTAGE, "mV"),
    Register("BatteryStatus", CMD_BATTERY_STATUS, "hex", False, lambda x: fmt_hex(x)),
    Register("Current", CMD_CURRENT, "mA", True),
    Register("RemainingCapacity", CMD_REMAINING_CAPACITY, "mAh"),
    Register("FullChargeCapacity", CMD_FULL_CHARGE_CAPACITY, "mAh"),
    Register("AverageCurrent", CMD_AVERAGE_CURRENT, "mA", True),
    Register("TimeToEmpty", CMD_TIME_TO_EMPTY, "min"),
    Register("TimeToFull", CMD_TIME_TO_FULL, "min"),
    Register("StandbyCurrent", CMD_STANDBY_CURRENT, "mA", True),
    Register("RawCoulombCount", CMD_RAW_COULOMB_COUNT, "raw"),
    Register("AveragePower", CMD_AVERAGE_POWER, "mW", True),
    Register("InternalTemperature", CMD_INTERNAL_TEMPERATURE, "degC", False, lambda x: round(x / 10.0 - 273.15, 2)),
    Register("CycleCount", CMD_CYCLE_COUNT, "cycles"),
    Register("StateOfCharge", CMD_STATE_OF_CHARGE, "%"),
    Register("StateOfHealth", CMD_STATE_OF_HEALTH, "%"),
    Register("ChargingVoltage", CMD_CHARGING_VOLTAGE, "mV"),
    Register("ChargingCurrent", CMD_CHARGING_CURRENT, "mA"),
    Register("OperationStatus", CMD_OPERATION_STATUS, "hex", False, lambda x: fmt_hex(x)),
    Register("DesignCapacity", CMD_DESIGN_CAPACITY, "mAh"),
]


class BQ27220:
    def __init__(self, bus_num: int, address: int, debug: bool = False, force: bool = False):
        if SMBus is None:
            raise BQError("missing SMBus module; install python3-smbus or smbus2")
        self.bus_num = bus_num
        self.address = address
        self.debug = debug
        self.force = force
        self.bus = SMBus(bus_num)

    def close(self) -> None:
        close = getattr(self.bus, "close", None)
        if close:
            close()

    def _dbg(self, msg: str) -> None:
        if self.debug:
            print(f"[debug] {msg}", file=sys.stderr)

    def _select_address(self) -> None:
        fd = getattr(self.bus, "fd", None)
        if fd is not None:
            fcntl.ioctl(fd, I2C_SLAVE_FORCE if self.force else I2C_SLAVE, self.address)

    def read_bytes(self, reg: int, length: int) -> bytes:
        self._select_address()
        fd = getattr(self.bus, "fd", None)
        if self.force and fd is not None:
            os.write(fd, bytes([reg]))
            time.sleep(0.001)
            return os.read(fd, length)
        if i2c_msg is not None:
            write = i2c_msg.write(self.address, [reg])
            read = i2c_msg.read(self.address, length)
            self.bus.i2c_rdwr(write, read)
            return bytes(read)
        data = self.bus.read_i2c_block_data(self.address, reg, length)
        return bytes(data)

    def write_bytes(self, reg: int, data: bytes | list[int]) -> None:
        self._select_address()
        payload = list(data)
        self._dbg(f"write reg=0x{reg:02X} data={' '.join(f'{x:02X}' for x in payload)}")
        fd = getattr(self.bus, "fd", None)
        if self.force and fd is not None:
            os.write(fd, bytes([reg] + payload))
        elif len(payload) == 1:
            self.bus.write_byte_data(self.address, reg, payload[0])
        else:
            self.bus.write_i2c_block_data(self.address, reg, payload)
        time.sleep(0.002)

    def read_word(self, reg: int, signed: bool = False) -> int:
        data = self.read_bytes(reg, 2)
        return le_i16(data) if signed else le_u16(data)

    def write_word(self, reg: int, value: int) -> None:
        self.write_bytes(reg, bytes([value & 0xFF, (value >> 8) & 0xFF]))

    def control(self, subcmd: int, wait: float = 0.05) -> None:
        self.write_word(CMD_CONTROL, subcmd)
        time.sleep(wait)

    def mac_command(self, subcmd: int, wait: float = 0.05) -> bytes:
        self.write_word(CMD_MANUFACTURER_ACCESS, subcmd)
        time.sleep(wait)
        data = self.read_bytes(CMD_MANUFACTURER_ACCESS, 32)
        if len(data) >= 2 and le_u16(data[:2]) == subcmd:
            return data[2:]
        return data

    def unseal_default(self) -> None:
        # BQ27220 default unseal key: write 0x0414 then 0x3672 to Control().
        self.control(0x0414, wait=0.05)
        self.control(0x3672, wait=0.05)

    def read_data_memory(self, address: int, length: int) -> bytes:
        self.write_bytes(CMD_MANUFACTURER_ACCESS, bytes([address & 0xFF, (address >> 8) & 0xFF]))
        time.sleep(0.01)
        # Some kernels/adapters return the 32-byte MACData payload from 0x40, while
        # forced raw I2C reads from 0x3E may include two command bytes first.
        data = self.read_bytes(CMD_MAC_DATA, max(32, length + 2))
        if len(data) >= length:
            return data[:length]
        data = self.read_bytes(CMD_MANUFACTURER_ACCESS, length + 2)
        if len(data) >= 2 and le_u16(data[:2]) == address:
            return data[2:2 + length]
        # Some adapters expose only the data payload for this path.
        return data[:length]

    def write_data_memory(self, address: int, payload: bytes) -> None:
        if not 1 <= len(payload) <= 32:
            raise BQError("data-memory payload must be 1..32 bytes")
        frame = bytes([address & 0xFF, (address >> 8) & 0xFF]) + payload
        checksum = (~sum(frame)) & 0xFF
        length = len(frame) + 2  # TI MACDataLen includes command bytes plus checksum/length bytes.
        self.write_bytes(CMD_MANUFACTURER_ACCESS, frame)
        self.write_bytes(CMD_MAC_DATA_SUM, bytes([checksum, length]))
        time.sleep(0.02)

    def read_field(self, name: str) -> int:
        address, kind, _unit, offset = DATA_FIELDS[name]
        size = 1 if kind.endswith("1") else 2
        return decode_value(kind, self.read_data_memory(address, offset + size)[offset:offset + size])

    def write_field(self, name: str, value: int) -> None:
        address, kind, _unit, _offset = DATA_FIELDS[name]
        self.write_data_memory(address, encode_value(kind, value))

    def enter_cfg_update(self) -> None:
        self.control(MAC_ENTER_CFG_UPDATE, wait=1.2)

    def exit_cfg_update(self, reinit: bool = True) -> None:
        self.control(MAC_EXIT_CFG_UPDATE_REINIT if reinit else MAC_EXIT_CFG_UPDATE, wait=1.2)


def battery_status_flags(value: int) -> list[str]:
    flags = []
    mapping = [
        (0, "DSG"), (1, "SYSDWN"), (2, "TDA"), (4, "BATTPRES"),
        (5, "AUTH_GD"), (6, "OCVGD"), (9, "CHGINH"), (10, "FC"),
        (11, "OTD"), (12, "OTC"), (13, "SLEEP"), (14, "OCVFAIL"),
        (15, "OCVCOMP"),
    ]
    for bit, name in mapping:
        if value & (1 << bit):
            flags.append(name)
    return flags


def operation_status_flags(value: int) -> list[str]:
    flags = []
    mapping = [
        (0, "CALMD"), (1, "SEC0"), (2, "SEC1"), (3, "EDV2"),
        (4, "VDQ"), (5, "INITCOMP"), (6, "SMTH"), (7, "BTPINT"),
        (10, "CFGUPDATE"),
    ]
    for bit, name in mapping:
        if value & (1 << bit):
            flags.append(name)
    return flags


def print_kv(name: str, value: object, unit: str = "") -> None:
    suffix = f" {unit}" if unit and unit not in ("hex", "raw") else ""
    print(f"{name:34s}: {value}{suffix}")


def show_registers(dev: BQ27220) -> None:
    print(f"BQ27220 on /dev/i2c-{dev.bus_num}, address 0x{dev.address:02X}")
    print("\n[standard registers]")
    for reg in REGISTERS:
        try:
            raw = dev.read_word(reg.cmd, signed=reg.signed)
            value = reg.transform(raw) if reg.transform else raw
            print_kv(reg.name, value, reg.unit)
            if reg.name == "BatteryStatus":
                print_kv("  BatteryStatus flags", ",".join(battery_status_flags(raw)) or "none")
            elif reg.name == "OperationStatus":
                print_kv("  OperationStatus flags", ",".join(operation_status_flags(raw)) or "none")
        except Exception as exc:
            print_kv(reg.name, f"read failed: {exc}")

    print("\n[device info]")
    for name, subcmd in (("Device Number", MAC_DEVICE_NUMBER), ("FW Version raw", MAC_FW_VERSION)):
        try:
            data = dev.mac_command(subcmd, wait=0.05)
            shown = " ".join(f"{b:02X}" for b in data[:16])
            if name == "Device Number" and len(data) >= 2:
                shown = f"0x{le_u16(data[:2]):04X} ({shown})"
            print_kv(name, shown)
        except Exception as exc:
            print_kv(name, f"read failed: {exc}")


def show_config(dev: BQ27220) -> None:
    print("\n[data memory / battery config]")
    for name, (_address, kind, unit, _offset) in DATA_FIELDS.items():
        try:
            value = dev.read_field(name)
            if kind == "h2":
                value = fmt_hex(value)
            print_kv(name, value, unit)
        except Exception as exc:
            print_kv(name, f"read failed: {exc}")


def init_device(dev: BQ27220, assume_unsealed: bool = False, reinit: bool = True) -> None:
    print("Entering CONFIG UPDATE and writing temporary battery settings...")
    if not assume_unsealed:
        dev.unseal_default()
    dev.enter_cfg_update()
    try:
        dev.control(MAC_SET_PROFILE_1, wait=0.05)
        for name, value in INIT_VALUES.items():
            dev.write_field(name, value)
            print_kv(f"wrote {name}", value, DATA_FIELDS[name][2])
    finally:
        dev.exit_cfg_update(reinit=reinit)

    try:
        dev.control(MAC_BAT_INSERT, wait=0.05)
    except Exception:
        pass
    print("Done. Note: setting is temporary unless OTP/programming flow is used.")



def poll(dev: BQ27220, interval: float) -> None:
    names = [
        ("Voltage", CMD_VOLTAGE, False, "mV"),
        ("Current", CMD_CURRENT, True, "mA"),
        ("AverageCurrent", CMD_AVERAGE_CURRENT, True, "mA"),
        ("RemainingCapacity", CMD_REMAINING_CAPACITY, False, "mAh"),
        ("FullChargeCapacity", CMD_FULL_CHARGE_CAPACITY, False, "mAh"),
        ("StateOfCharge", CMD_STATE_OF_CHARGE, False, "%"),
        ("StateOfHealth", CMD_STATE_OF_HEALTH, False, "%"),
        ("Temperature", CMD_TEMPERATURE, False, "degC"),
        ("BatteryStatus", CMD_BATTERY_STATUS, False, "hex"),
    ]
    print("time                 voltage current avg_current rem_cap full_cap soc soh temp status")
    while True:
        values: dict[str, object] = {}
        for name, cmd, signed, unit in names:
            try:
                raw = dev.read_word(cmd, signed=signed)
                if name == "Temperature":
                    values[name] = f"{raw / 10.0 - 273.15:.1f}C"
                elif unit == "hex":
                    values[name] = fmt_hex(raw)
                else:
                    values[name] = f"{raw}{unit}"
            except Exception as exc:
                values[name] = f"ERR:{exc}"
        print(
            f"{time.strftime('%Y-%m-%d %H:%M:%S')} "
            f"{values['Voltage']:>8} {values['Current']:>8} {values['AverageCurrent']:>11} "
            f"{values['RemainingCapacity']:>8} {values['FullChargeCapacity']:>8} "
            f"{values['StateOfCharge']:>5} {values['StateOfHealth']:>5} "
            f"{values['Temperature']:>6} {values['BatteryStatus']:>6}",
            flush=True,
        )
        time.sleep(interval)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Read/configure/monitor TI BQ27220 over Linux i2c-dev")
    parser.add_argument("--bus", type=parse_int_auto, default=env_int(("BQ27220_BUS", "BQ_BUS", "I2C_BUS"), DEFAULT_BUS),
                        help="I2C bus number, env BQ27220_BUS/BQ_BUS/I2C_BUS, default 1")
    parser.add_argument("--addr", type=parse_int_auto, default=env_int(("BQ27220_ADDR", "BQ_ADDR", "I2C_ADDR"), DEFAULT_ADDR),
                        help="7-bit I2C address, env BQ27220_ADDR/BQ_ADDR/I2C_ADDR, default 0x55")
    parser.add_argument("--debug", action="store_true", help="print I2C write frames to stderr")
    parser.add_argument("--force", action="store_true", help="force I2C access even if a kernel driver owns the address")
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("show", help="read registers and current data-memory battery config")

    init_p = sub.add_parser("init", help="temporarily initialize battery settings in BQ27220 RAM data memory")
    init_p.add_argument("--assume-unsealed", action="store_true", help="skip default unseal sequence")
    init_p.add_argument("--no-reinit", action="store_true", help="exit CONFIG UPDATE without reinitialize")

    poll_p = sub.add_parser("poll", help="print live battery data repeatedly")
    poll_p.add_argument("--interval", "-i", type=float, default=1.0, help="poll interval seconds, default 1")

    sub.add_parser("read", help="same as show")
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)
    command = args.command or "show"
    try:
        dev = BQ27220(args.bus, args.addr, debug=args.debug, force=args.force)
    except Exception as exc:
        print(f"error: cannot open BQ27220 on /dev/i2c-{args.bus} addr 0x{args.addr:02X}: {exc}", file=sys.stderr)
        return 2

    try:
        if command in ("show", "read"):
            show_registers(dev)
            show_config(dev)
        elif command == "init":
            init_device(dev, assume_unsealed=args.assume_unsealed, reinit=not args.no_reinit)
            show_registers(dev)
            show_config(dev)
        elif command == "poll":
            poll(dev, args.interval)
        else:
            raise BQError(f"unknown command {command}")
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        dev.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
