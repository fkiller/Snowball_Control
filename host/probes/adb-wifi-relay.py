"""Loopback-only ADB relay for a dual-homed PC. No global route changes."""
import socket, threading

def get_wifi_ip():
    try:
        import subprocess
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", "(Get-NetIPAddress -InterfaceAlias 'Wi-Fi' -AddressFamily IPv4).IPAddress"],
            text=True
        )
        ip = out.strip().split()[0]
        if ip: return ip
    except Exception:
        pass
    return '192.168.1.200'

def connection(client):
    upstream = socket.socket()
    try:
        upstream.settimeout(5)
        upstream.bind((get_wifi_ip(), 0))
        upstream.connect(('192.168.1.248', 5555))
        upstream.settimeout(None)
        def pump(source, destination):
            try:
                while data := source.recv(65536): destination.sendall(data)
            except OSError: pass
            finally:
                try: destination.shutdown(socket.SHUT_WR)
                except OSError: pass
        worker = threading.Thread(target=pump,args=(client,upstream),daemon=True)
        worker.start(); pump(upstream,client); worker.join()
    finally: client.close(); upstream.close()

with socket.socket() as listener:
    listener.bind(('127.0.0.1', 15555)); listener.listen()
    print('ADB relay ready on 127.0.0.1:15555 via Wi-Fi', flush=True)
    while True:
        client,_=listener.accept()
        threading.Thread(target=connection,args=(client,),daemon=True).start()
