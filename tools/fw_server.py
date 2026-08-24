#!/usr/bin/env python3
"""
FlashSafe Pro - Firmware HTTP Server
Simple HTTP server with Range request support for firmware updates.
"""

import os
import sys
import json
import argparse
import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, unquote
import mimetypes

class FirmwareHTTPHandler(BaseHTTPRequestHandler):
    """HTTP handler with Range request support."""
    
    server_version = "FlashSafe/1.0"
    
    def log_message(self, format, *args):
        """Override to add timestamp to logs."""
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        sys.stdout.write(f"[{timestamp}] {self.address_string()} - {format % args}\n")
        sys.stdout.flush()
    
    def do_HEAD(self):
        """Handle HEAD requests."""
        self._handle_request(head_only=True)
    
    def do_GET(self):
        """Handle GET requests."""
        self._handle_request(head_only=False)
    
    def _handle_request(self, head_only=False):
        """Handle GET/HEAD requests with Range support."""
        parsed_path = urlparse(self.path)
        path = unquote(parsed_path.path)
        
        # Remove leading slash
        if path.startswith('/'):
            path = path[1:]
        
        # Handle version.json endpoint
        if path == 'version.json':
            self._handle_version_json(head_only)
            return
        
        # Serve static files
        if not path or path == '/':
            path = 'index.html'
        
        file_path = os.path.join(self.directory, path)
        
        # Security check - prevent directory traversal
        if not os.path.abspath(file_path).startswith(os.path.abspath(self.directory)):
            self.send_error(403, "Forbidden")
            return
        
        if not os.path.exists(file_path):
            self.send_error(404, "File Not Found")
            return
        
        if not os.path.isfile(file_path):
            self.send_error(403, "Forbidden")
            return
        
        # Get file size
        file_size = os.path.getsize(file_path)
        
        # Check for Range header
        range_header = self.headers.get('Range')
        
        if range_header:
            self._handle_range_request(file_path, file_size, range_header, head_only)
        else:
            self._send_file(file_path, file_size, head_only)
    
    def _handle_range_request(self, file_path, file_size, range_header, head_only):
        """Handle Range requests."""
        try:
            # Parse Range header (bytes=start-end)
            range_spec = range_header.split('=')[1]
            range_parts = range_spec.split('-')
            
            start = int(range_parts[0]) if range_parts[0] else 0
            end = int(range_parts[1]) if range_parts[1] else file_size - 1
            
            # Validate range
            if start < 0 or end >= file_size or start > end:
                self.send_response(416, "Range Not Satisfiable")
                self.send_header('Content-Range', f'bytes */{file_size}')
                self.end_headers()
                return
            
            content_length = end - start + 1
            
            self.send_response(206, "Partial Content")
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(content_length))
            self.send_header('Content-Range', f'bytes {start}-{end}/{file_size}')
            self.send_header('Accept-Ranges', 'bytes')
            self.end_headers()
            
            if not head_only:
                with open(file_path, 'rb') as f:
                    f.seek(start)
                    data = f.read(content_length)
                    self.wfile.write(data)
        
        except (ValueError, IndexError) as e:
            self.send_error(400, f"Invalid Range header: {e}")
    
    def _send_file(self, file_path, file_size, head_only):
        """Send complete file."""
        content_type = mimetypes.guess_type(file_path)[0] or 'application/octet-stream'
        
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(file_size))
        self.send_header('Accept-Ranges', 'bytes')
        self.end_headers()
        
        if not head_only:
            with open(file_path, 'rb') as f:
                while True:
                    chunk = f.read(8192)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
    
    def _handle_version_json(self, head_only):
        """Handle version.json endpoint."""
        # Look for version.json in the directory
        version_file = os.path.join(self.directory, 'version.json')
        
        if os.path.exists(version_file):
            self._send_file(version_file, os.path.getsize(version_file), head_only)
        else:
            # Return default version info
            version_info = {
                "version": "0.0.0",
                "description": "No version information available",
                "timestamp": datetime.datetime.now().isoformat()
            }
            
            content = json.dumps(version_info, indent=2).encode('utf-8')
            
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(content)))
            self.end_headers()
            
            if not head_only:
                self.wfile.write(content)

class FirmwareHTTPServer(HTTPServer):
    """HTTP server with directory configuration."""
    
    def __init__(self, server_address, handler_class, directory):
        self.directory = os.path.abspath(directory)
        super().__init__(server_address, handler_class)
        
        # Add directory to handler class
        handler_class.directory = self.directory
        
        print(f"Serving files from: {self.directory}")
        print(f"Version endpoint: http://{server_address[0]}:{server_address[1]}/version.json")

def run_server(port, directory):
    """Run the firmware HTTP server."""
    if not os.path.exists(directory):
        print(f"Error: Directory not found: {directory}")
        sys.exit(1)
    
    if not os.path.isdir(directory):
        print(f"Error: Not a directory: {directory}")
        sys.exit(1)
    
    server_address = ('0.0.0.0', port)
    httpd = FirmwareHTTPServer(server_address, FirmwareHTTPHandler, directory)
    
    print(f"\nFlashSafe Pro Firmware Server")
    print(f"Listening on: http://0.0.0.0:{port}")
    print(f"Press Ctrl+C to stop\n")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.shutdown()

def main():
    parser = argparse.ArgumentParser(
        description="FlashSafe Pro Firmware HTTP Server"
    )
    parser.add_argument(
        "--port", "-p",
        type=int,
        default=8080,
        help="Port to listen on (default: 8080)"
    )
    parser.add_argument(
        "--dir", "-d",
        default="./firmware",
        help="Directory to serve files from (default: ./firmware)"
    )
    
    args = parser.parse_args()
    
    if args.port < 1 or args.port > 65535:
        print("Error: Port must be between 1 and 65535")
        sys.exit(1)
    
    run_server(args.port, args.dir)

if __name__ == "__main__":
    main()