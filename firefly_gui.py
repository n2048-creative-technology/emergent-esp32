#!/usr/bin/env python3
"""
Firefly Proximity Network - Cellular Automata GUI

A simple GUI for configuring kernel and activation parameters,
monitoring serial output from ESP32-C3 devices running the Firefly firmware.
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import re


class FireflyGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Firefly CA - Kernel & Activation Configurator")
        self.root.geometry("900x700")
        
        self.serial_port = None
        self.serial_thread = None
        self.stop_thread = False
        self.message_queue = queue.Queue()
        
        self.create_widgets()
        self.list_ports()
        self.root.after(100, self.process_queue)
    
    def create_widgets(self):
        # Main notebook for tabs
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True)
        
        # Serial tab
        self.serial_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.serial_frame, text="Serial Monitor")
        self.create_serial_tab()
        
        # Kernel tab
        self.kernel_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.kernel_frame, text="Kernel")
        self.create_kernel_tab()
        
        # Activations tab
        self.activations_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.activations_frame, text="Activations")
        self.create_activations_tab()
        
        # Presets tab
        self.presets_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.presets_frame, text="Presets")
        self.create_presets_tab()
    
    def create_serial_tab(self):
        frame = self.serial_frame
        
        # Port selection
        port_frame = ttk.LabelFrame(frame, text="Serial Port")
        port_frame.pack(fill=tk.X, padx=5, pady=5)
        
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_frame, textvariable=self.port_var, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=5)
        
        self.refresh_btn = ttk.Button(port_frame, text="Refresh", command=self.list_ports)
        self.refresh_btn.pack(side=tk.LEFT, padx=5)
        
        self.connect_btn = ttk.Button(port_frame, text="Connect", command=self.connect_serial)
        self.connect_btn.pack(side=tk.LEFT, padx=5)
        
        self.disconnect_btn = ttk.Button(port_frame, text="Disconnect", command=self.disconnect_serial, state=tk.DISABLED)
        self.disconnect_btn.pack(side=tk.LEFT, padx=5)
        
        # Serial monitor
        monitor_frame = ttk.LabelFrame(frame, text="Serial Output")
        monitor_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.serial_text = scrolledtext.ScrolledText(monitor_frame, wrap=tk.WORD, state=tk.DISABLED)
        self.serial_text.pack(fill=tk.BOTH, expand=True)
        
        # Clear button
        clear_btn = ttk.Button(monitor_frame, text="Clear", command=self.clear_serial)
        clear_btn.pack(side=tk.RIGHT, padx=5, pady=2)
    
    def create_kernel_tab(self):
        frame = self.kernel_frame
        
        help_text = tk.Label(frame, text="Set 9 kernel weights (k0-k8 for neighbors + self)")
        help_text.pack(pady=5)
        
        self.kernel_entries = []
        kernel_frame = ttk.Frame(frame)
        kernel_frame.pack(fill=tk.X, padx=5, pady=5)
        
        for i in range(9):
            label = ttk.Label(kernel_frame, text=f"k{i}:")
            label.grid(row=0, column=i*2, padx=2)
            entry = ttk.Entry(kernel_frame, width=8)
            entry.grid(row=0, column=i*2+1, padx=2)
            entry.insert(0, "0.0")
            self.kernel_entries.append(entry)
        
        # Preset kernel buttons
        preset_kernel_frame = ttk.Frame(frame)
        preset_kernel_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Button(preset_kernel_frame, text="Set All to 1", 
                  command=lambda: self.set_all_kernel(1.0)).pack(side=tk.LEFT, padx=2)
        ttk.Button(preset_kernel_frame, text="Set All to 0", 
                  command=lambda: self.set_all_kernel(0.0)).pack(side=tk.LEFT, padx=2)
        ttk.Button(preset_kernel_frame, text="Neighbors Only (self=0)", 
                  command=lambda: self.set_kernel_preset("neighbors")).pack(side=tk.LEFT, padx=2)
        
        # Send button
        send_kernel_btn = ttk.Button(frame, text="Send Kernel to Device", 
                                     command=self.send_kernel)
        send_kernel_btn.pack(pady=10)
    
    def create_activations_tab(self):
        frame = self.activations_frame
        
        help_text = tk.Label(frame, text="Set activation conditions (op, value pairs)")
        help_text.pack(pady=5)
        help_text2 = tk.Label(frame, text="Op: 0=<, 1<=, 2==, 3>=, 4>")
        help_text2.pack(pady=2)
        
        # Activation list frame
        self.activation_frames = []
        self.activation_container = ttk.Frame(frame)
        self.activation_container.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Add first activation
        self.add_activation()
        
        # Buttons
        btn_frame = ttk.Frame(frame)
        btn_frame.pack(fill=tk.X, padx=5, pady=5)
        
        ttk.Button(btn_frame, text="Add Activation", command=self.add_activation).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_frame, text="Remove Last", command=self.remove_activation).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_frame, text="Clear All", command=self.clear_activations).pack(side=tk.LEFT, padx=2)
        
        # Send button
        send_activations_btn = ttk.Button(frame, text="Send Activations to Device", 
                                         command=self.send_activations)
        send_activations_btn.pack(pady=10)
    
    def create_presets_tab(self):
        frame = self.presets_frame
        
        help_text = tk.Label(frame, text="Load predefined cellular automata configurations")
        help_text.pack(pady=10)
        
        self.preset_var = tk.StringVar()
        preset_combo = ttk.Combobox(frame, textvariable=self.preset_var, state="readonly")
        preset_combo['values'] = ['conway', 'rule30', 'majority', 'and', 'or']
        preset_combo.pack(pady=5)
        preset_combo.set('conway')
        
        # Description
        self.preset_desc = tk.Label(frame, text="", wraplength=400)
        self.preset_desc.pack(pady=5)
        
        # Update description when preset changes
        preset_combo.bind('<<ComboboxSelected>>', self.update_preset_description)
        
        # Load preset button
        load_btn = ttk.Button(frame, text="Load Preset", command=self.load_preset)
        load_btn.pack(pady=10)
        
        # Send preset button
        send_btn = ttk.Button(frame, text="Send Preset to Device", command=self.send_preset)
        send_btn.pack(pady=5)
        
        # Also add manual commands
        ttk.Label(frame, text="Or send manual command:").pack(pady=10)
        self.manual_cmd = ttk.Entry(frame, width=50)
        self.manual_cmd.pack(pady=5)
        self.manual_cmd.insert(0, "reset")
        ttk.Button(frame, text="Send Command", command=self.send_manual_command).pack(pady=5)
    
    def list_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()
                 if 'ttyACM' in p.device or 'ttyUSB' in p.device]
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.set(ports[0])
    
    def connect_serial(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Error", "No port selected")
            return
        
        try:
            self.serial_port = serial.Serial(port, baudrate=115200, timeout=0.1)
            self.connect_btn.config(state=tk.DISABLED)
            self.disconnect_btn.config(state=tk.NORMAL)
            self.append_serial(f"Connected to {port}\n")
            
            self.stop_thread = False
            self.serial_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.serial_thread.start()
        except Exception as e:
            messagebox.showerror("Error", f"Failed to connect: {e}")
    
    def disconnect_serial(self):
        self.stop_thread = True
        if self.serial_thread:
            self.serial_thread.join(timeout=1)
        if self.serial_port:
            self.serial_port.close()
            self.serial_port = None
        self.connect_btn.config(state=tk.NORMAL)
        self.disconnect_btn.config(state=tk.DISABLED)
        self.append_serial("Disconnected\n")
    
    def read_serial(self):
        while not self.stop_thread and self.serial_port:
            try:
                if self.serial_port.in_waiting:
                    line = self.serial_port.readline().decode('utf-8', errors='ignore')
                    if line:
                        self.message_queue.put(line)
            except Exception:
                pass
    
    def process_queue(self):
        while not self.message_queue.empty():
            msg = self.message_queue.get()
            self.append_serial(msg)
        self.root.after(100, self.process_queue)
    
    def append_serial(self, text):
        self.serial_text.config(state=tk.NORMAL)
        self.serial_text.insert(tk.END, text)
        self.serial_text.see(tk.END)
        self.serial_text.config(state=tk.DISABLED)
    
    def clear_serial(self):
        self.serial_text.config(state=tk.NORMAL)
        self.serial_text.delete(1.0, tk.END)
        self.serial_text.config(state=tk.DISABLED)
    
    def send_command(self, command):
        if not self.serial_port:
            messagebox.showerror("Error", "Not connected to a device")
            return
        try:
            self.serial_port.write((command + '\n').encode('utf-8'))
            self.append_serial(f"Sent: {command}\n")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to send: {e}")
    
    def send_kernel(self):
        values = [e.get() for e in self.kernel_entries]
        try:
            cmd = "kernel " + ",".join(values)
            self.send_command(cmd)
        except Exception as e:
            messagebox.showerror("Error", f"Invalid kernel values: {e}")
    
    def set_all_kernel(self, value):
        for e in self.kernel_entries:
            e.delete(0, tk.END)
            e.insert(0, str(value))
    
    def set_kernel_preset(self, preset):
        if preset == "neighbors":
            for i, e in enumerate(self.kernel_entries):
                e.delete(0, tk.END)
                e.insert(0, "1.0" if i < 8 else "0.0")
    
    def add_activation(self):
        idx = len(self.activation_frames)
        frame = ttk.Frame(self.activation_container)
        frame.pack(fill=tk.X, pady=2)
        
        op_label = ttk.Label(frame, text=f"Op:")
        op_label.pack(side=tk.LEFT, padx=2)
        op_entry = ttk.Entry(frame, width=5)
        op_entry.pack(side=tk.LEFT, padx=2)
        op_entry.insert(0, "2")  # Default to ==
        
        val_label = ttk.Label(frame, text="Value:")
        val_label.pack(side=tk.LEFT, padx=2)
        val_entry = ttk.Entry(frame, width=10)
        val_entry.pack(side=tk.LEFT, padx=2)
        val_entry.insert(0, "0.0")
        
        self.activation_frames.append((frame, op_entry, val_entry))
    
    def remove_activation(self):
        if self.activation_frames:
            frame, _, _ = self.activation_frames.pop()
            frame.destroy()
    
    def clear_activations(self):
        for frame, _, _ in self.activation_frames:
            frame.destroy()
        self.activation_frames = []
        self.add_activation()
    
    def send_activations(self):
        cmd_parts = []
        for _, op_e, val_e in self.activation_frames:
            op = op_e.get()
            val = val_e.get()
            cmd_parts.append(f"{op},{val}")
        
        if cmd_parts:
            cmd = "activations " + ",".join(cmd_parts)
            self.send_command(cmd)
    
    def update_preset_description(self, event=None):
        preset = self.preset_var.get()
        descriptions = {
            'conway': "Conway's Game of Life: Born at exactly 3 neighbors, survives at 2-3 neighbors",
            'rule30': "Elementary Cellular Automaton Rule 30 (simplified for 2D)",
            'majority': "Majority vote: 1 if at least 5 of 8 neighbors are 1",
            'and': "AND gate: 1 only if ALL 8 neighbors are 1",
            'or': "OR gate: 1 if ANY neighbor is 1"
        }
        self.preset_desc.config(text=descriptions.get(preset, ""))
    
    def load_preset(self):
        preset = self.preset_var.get()
        
        # Kernel presets
        kernels = {
            'conway': [1, 1, 1, 1, 1, 1, 1, 1, 10.0],
            'rule30': [1, 1, 1, 0, 0, 0, 0, 0, 0],
            'majority': [1, 1, 1, 1, 1, 1, 1, 1, 0],
            'and': [1, 1, 1, 1, 1, 1, 1, 1, 0],
            'or': [1, 1, 1, 1, 1, 1, 1, 1, 0]
        }
        
        if preset in kernels:
            for i, e in enumerate(self.kernel_entries):
                e.delete(0, tk.END)
                e.insert(0, str(kernels[preset][i]))
        
        # Activation presets
        activations = {
            'conway': [(2, 3.0), (2, 12.0), (2, 13.0)],
            'rule30': [(2, 1.0), (2, 2.0)],
            'majority': [(3, 4.5)],
            'and': [(2, 8.0)],
            'or': [(3, 0.5)]
        }
        
        self.clear_activations()
        if preset in activations:
            for op, val in activations[preset]:
                self.add_activation()
                _, op_e, val_e = self.activation_frames[-1]
                op_e.delete(0, tk.END)
                op_e.insert(0, str(op))
                val_e.delete(0, tk.END)
                val_e.insert(0, str(val))
    
    def send_preset(self):
        preset = self.preset_var.get()
        self.send_command(f"preset {preset}")
    
    def send_manual_command(self):
        cmd = self.manual_cmd.get()
        if cmd:
            self.send_command(cmd)


if __name__ == "__main__":
    root = tk.Tk()
    app = FireflyGUI(root)
    root.mainloop()
