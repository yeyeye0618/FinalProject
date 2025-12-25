import subprocess
import time
import os
import socket

# Configuration
SERVER_BIN = os.path.join("bin", "server")
CLIENT_BIN = os.path.join("bin", "client")
SERVER_PORT = 8080
LOG_FILE = "test_run.log"

def log(message):
    print(f"[TEST RUNNER] {message}")

def get_server_path():
    if os.path.exists(SERVER_BIN):
        return SERVER_BIN
    if os.path.exists(SERVER_BIN + ".exe"):
        return SERVER_BIN + ".exe"
    return None

def get_client_path():
    if os.path.exists(CLIENT_BIN):
        return CLIENT_BIN
    if os.path.exists(CLIENT_BIN + ".exe"):
        return CLIENT_BIN + ".exe"
    return None

def start_server(env=None):
    server_path = get_server_path()
    if not server_path:
        log(f"Error: Server binary not found at {SERVER_BIN}")
        return None

    log(f"Starting Server (Env: {env})...")
    
    server_out = open("server_output.log", "a") 
    
    server_process = subprocess.Popen(
        [server_path],
        stdout=server_out,
        stderr=subprocess.STDOUT,
        env=env
    )
    time.sleep(2) 
    
    if server_process.poll() is not None:
        log("Server failed to start.")
        return None
        
    return server_process

def stop_server(server_process):
    if server_process:
        log("Stopping Server...")
        server_process.terminate()
        try:
            server_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server_process.kill()
        log("Server stopped.")

def run_client(num_threads, action, *args, expect_fail=False):
    client_path = get_client_path()
    cmd = [client_path, str(num_threads), action] + list(args)
    
    try:
        start_time = time.time()
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=15 
        )
        duration = time.time() - start_time
        
        print("--- Client Output ---")
        lines = result.stdout.splitlines()
        for line in lines[:5]:
            print(line)
        if len(lines) > 5:
            print(f"... ({len(lines)-5} more lines)")
        
        if result.returncode != 0:
            print("--- Client Error (Stderr) ---")
            print(result.stderr)
        
        if expect_fail:
            if result.returncode != 0:
                log(f"Client failed as expected (Return Code: {result.returncode}, Duration: {duration:.2f}s).")
            else:
                log(f"FAILURE: Client succeeded but should have failed!")
        else:
            if result.returncode == 0:
                log("Client finished successfully.")
            else:
                log(f"FAILURE: Client failed with return code {result.returncode}")

    except subprocess.TimeoutExpired:
        log("Client Execution Timed Out (Process killed by test runner)!")
    except Exception as e:
        log(f"Client execution error: {e}")

def run_functional_tests():
    log("=== Running Functional Tests ===")
    
    server_proc = start_server()
    if not server_proc: return

    try:
        log("Test 1: Query Availability (1 thread)...")
        run_client(1, "query")

        log("Test 2: Book 1 Ticket (1 thread)...")
        run_client(1, "book", "1")

        log("Test 3: Concurrent Booking (10 threads, 1 ticket each)...")
        run_client(10, "book", "1")

        log("Test 4: Stress Test (100 threads, query)...")
        run_client(100, "query")

    finally:
        stop_server(server_proc)

def run_client_timeout_test():
    log("\n=== Running Client Timeout Test (Mock Server) ===")
    log("Objective: Verify Client times out when Server is slow (7s delay).")
    
    # 1. Create a Fake Slow Server
    mock_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    mock_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        mock_socket.bind(('0.0.0.0', SERVER_PORT))
        mock_socket.listen(1)
        log(f"Mock Server listening on port {SERVER_PORT}...")
    except Exception as e:
        log(f"Failed to start Mock Server: {e}")
        return

    # 2. Run Client in background
    log("Starting Client...")
    
    # We must run client in a different thread or use subprocess.Popen non-blocking
    # Because we need to accept() in the main thread (or vice-verse)
    # Easiest is to use existing run_client logic but just call it.
    # But run_client uses subprocess.run which blocks.
    # So we need to accept FIRST? No, bind/listen is non-blocking setup, but accept() blocks.
    # So:
    #   Step A: Bind/Listen
    #   Step B: Start Client Process (Blocking? No, Popen!)
    #   Step C: Accept Connection
    #   Step D: Sleep
    
    # Let's start Client in a subprocess.Popen manually to avoid blocking here
    client_path = get_client_path()
    client_cmd = [client_path, "1", "query"]
    
    client_proc = subprocess.Popen(
        client_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    # 3. Accept Connection
    try:
        mock_socket.settimeout(5) # Wait at most 5s for client to connect
        conn, addr = mock_socket.accept()
        log(f"Mock Server accepted connection from {addr}")
        
        # 4. Sleep longer than Client's timeout (5s)
        log("Mock Server sleeping for 7 seconds...")
        time.sleep(7)
        
        # 5. Close
        conn.close()
        
    except socket.timeout:
        log("FAILURE: Client did not connect to Mock Server.")
    except Exception as e:
        log(f"Mock Server Error: {e}")
    finally:
        mock_socket.close()
    
    # Check Client Exit Code
    try:
        stdout, stderr = client_proc.communicate(timeout=2)
        log(f"Client Return Code: {client_proc.returncode}")
        if client_proc.returncode != 0:
            log("SUCCESS: Client failed/timed out as expected.")
        else:
            log("FAILURE: Client succeeded unexpectedly!")
    except subprocess.TimeoutExpired:
        log("Client still running? Killing it.")
        client_proc.kill()
        log("FAILURE: Client hung indefinitely (should have timed out internally).")


def run_server_timeout_test():
    log("\n=== Running Server Timeout Test (Mock Client) ===")
    log("Objective: Verify Server disconnects idle Client after 10s.")
    
    # 1. Start Real Server
    server_proc = start_server()
    if not server_proc: return

    try:
        # 2. Connect Mock Client
        log("Mock Client connecting...")
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', SERVER_PORT))
        log("Mock Client connected.")
        
        # 3. Sleep longer than Server's timeout (10s)
        log("Mock Client sleeping for 12 seconds...")
        time.sleep(12)
        
        # 4. Attempt to send data
        log("Mock Client attempting to send data...")
        try:
            s.send(b'test')
            # If send succeeds, maybe server hasn't detected yet (TCP logic). 
            # Try receiving, which should trigger error if closed.
            data = s.recv(1024)
            if not data:
                log("SUCCESS: Server closed connection (recv returned empty).")
            else:
                log(f"FAILURE: Server sent data: {data}")
        except (ConnectionResetError, BrokenPipeError):
             log("SUCCESS: Connection reset/broken pipe (Server disconnected us).")
        except Exception as e:
            log(f"Unexpected connection state: {e}")
            
        s.close()
        
    finally:
        stop_server(server_proc)

if __name__ == "__main__":
    if os.path.exists("server_output.log"): os.remove("server_output.log")
    
    run_functional_tests()
    run_client_timeout_test()
    run_server_timeout_test()
