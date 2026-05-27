import tkinter as tk
from tkinter import filedialog, ttk
import asyncio
from bleak import BleakClient, BleakScanner

DFU_CONTROL_UUID = "8ec90001-f315-4f60-9fb8-838830daea50"
DFU_PACKET_UUID  = "8ec90002-f315-4f60-9fb8-838830daea50"

class DFUGui:
    def __init__(self, root):
        self.root = root
        self.root.title("Black-Valetudo OTA Update")
        self.root.geometry("500x350")
        self.firmware_path = None
        self._build_ui()

    def _build_ui(self):
        tk.Label(self.root, text="Black-Valetudo OTA Update",
                 font=("Arial", 16)).pack(pady=10)

        tk.Label(self.root, text="Select Device:").pack()
        self.device_var = tk.StringVar()
        self.device_dropdown = ttk.Combobox(self.root,
                                             textvariable=self.device_var,
                                             width=40)
        self.device_dropdown.pack(pady=5)

        tk.Button(self.root, text="Scan for Devices",
                  command=self.scan_devices).pack(pady=5)

        tk.Button(self.root, text="Select Firmware (.zip)",
                  command=self.select_firmware).pack(pady=5)

        self.firmware_label = tk.Label(self.root, text="No firmware selected")
        self.firmware_label.pack()

        self.status_label = tk.Label(self.root, text="Ready")
        self.status_label.pack(pady=10)

        tk.Button(self.root, text="Start OTA Update",
                  command=self.start_update,
                  bg="green", fg="white").pack(pady=10)

    def scan_devices(self):
        self.status_label.config(text="Scanning...")
        self.root.update()

        devices = asyncio.run(BleakScanner.discover(timeout=5.0))
        device_list = [f"{d.name} ({d.address})" for d in devices
                      if d.name]

        self.device_dropdown['values'] = device_list
        self.status_label.config(text=f"Found {len(device_list)} devices")

    def select_firmware(self):
        path = filedialog.askopenfilename(
            filetypes=[("DFU Package", "*.zip")]
        )
        if path:
            self.firmware_path = path
            self.firmware_label.config(text=path.split("/")[-1])

    def start_update(self):
        if not self.firmware_path:
            self.status_label.config(text="Please select firmware first")
            return

        if not self.device_var.get():
            self.status_label.config(text="Please select a device first")
            return

        addr = self.device_var.get().split("(")[-1].strip(")")
        self.status_label.config(text="Starting OTA update...")
        self.root.update()

        asyncio.run(self._perform_dfu(addr))

    async def _perform_dfu(self, addr):
        import zipfile
        import json
        import struct

        async with BleakClient(addr) as client:
            self.status_label.config(text="Connected, starting DFU...")
            self.root.update()

            with zipfile.ZipFile(self.firmware_path) as z:
                manifest  = json.loads(z.read("manifest.json"))
                bin_file  = manifest["manifest"]["application"]["bin_file"]
                dat_file  = manifest["manifest"]["application"]["dat_file"]
                firmware  = z.read(bin_file)
                init_data = z.read(dat_file)

            await client.write_gatt_char(DFU_CONTROL_UUID,
                                          bytes([0x01, 0x04]),
                                          response=True)
            await client.write_gatt_char(DFU_CONTROL_UUID,
                                          struct.pack("<H", len(init_data)),
                                          response=True)
            await client.write_gatt_char(DFU_PACKET_UUID,
                                          init_data,
                                          response=False)

            chunk_size = 20
            for i in range(0, len(firmware), chunk_size):
                chunk = firmware[i:i + chunk_size]
                await client.write_gatt_char(DFU_PACKET_UUID,
                                              chunk,
                                              response=False)

            await client.write_gatt_char(DFU_CONTROL_UUID,
                                          bytes([0x03]),
                                          response=True)
            await client.write_gatt_char(DFU_CONTROL_UUID,
                                          bytes([0x05]),
                                          response=True)

            self.status_label.config(text="OTA update complete!")
            self.root.update()

if __name__ == "__main__":
    root = tk.Tk()
    app = DFUGui(root)
    root.mainloop()
