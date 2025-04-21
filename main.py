from flask import Flask, request, jsonify, session
from flask_mysqldb import MySQL
import MySQLdb.cursors, re, hashlib
import os
from flask_cors import CORS  # Import CORS
from datetime import timedelta  # Import timedelta for session expiration
from flask_login import LoginManager, UserMixin, login_user, logout_user, login_required, current_user
import ssl
import time

app = Flask(__name__)

CORS(app, supports_credentials=True)

# Update session cookie configurations
app.config['SESSION_COOKIE_SAMESITE'] = 'None'  # Use 'Lax' for better compatibility
app.config['SESSION_COOKIE_SECURE'] = True    # Set to False for local development (no HTTPS)
app.config['SESSION_COOKIE_DOMAIN'] = None     # Use None for localhost
app.config['SESSION_COOKIE_HTTPONLY'] = True   # Ensure cookies are HTTP-only

# Load configuration from config.py if it exists, otherwise use default values
if os.path.exists('config.py'):
    app.config.from_pyfile('config.py')
else:
    app.secret_key = 'your secret key'
    app.config['MYSQL_HOST'] = 'localhost'
    app.config['MYSQL_USER'] = 'root'
    app.config['MYSQL_PASSWORD'] = 'root'
    app.config['MYSQL_DB'] = 'pythonlogin'

# Set session lifetime
app.permanent_session_lifetime = timedelta(days=7)  # Session lasts for 7 days

# Intialize MySQL
mysql = MySQL(app)

# Initialize Flask-Login
login_manager = LoginManager()
login_manager.init_app(app)
login_manager.login_view = 'login'

# User class for Flask-Login
class User(UserMixin):
    def __init__(self, id, username):
        self.id = id
        self.username = username

# User loader for Flask-Login
@login_manager.user_loader
def load_user(user_id):
    cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
    cursor.execute('SELECT * FROM accounts WHERE id = %s', (user_id,))
    account = cursor.fetchone()
    if account:
        return User(id=account['id'], username=account['username'])
    return None

@app.route('/login', methods=['POST'])
def login():
    if request.method == 'POST' and 'username' in request.json and 'password' in request.json:
        username = request.json['username']
        password = request.json['password']

        # Hash the entered password
        hash = password + app.secret_key
        hash = hashlib.sha1(hash.encode())
        hashed_password = hash.hexdigest()

        # Check if account exists
        cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
        cursor.execute('SELECT * FROM accounts WHERE username = %s AND password = %s', (username, hashed_password,))
        account = cursor.fetchone()
        if account:
            user = User(id=account['id'], username=account['username'])
            login_user(user)
            return jsonify({"message": "Login successful", "user_id": account['id'], "username": account['username']}), 200
        else:
            return jsonify({"error": "Incorrect username/password"}), 401
    return jsonify({"error": "Invalid request"}), 400

@app.route('/logout', methods=['POST'])
def logout():
    logout_user()  # Logs out the user using Flask-Login

    # Clear the session cookies
    session.clear()

    response = jsonify({"message": "Logged out successfully"})
    response.set_cookie('session', '', expires=0)  # Delete the session cookie
    return response, 200

@app.route('/register', methods=['POST'])
def register():
    # Check if "username", "password" and "email" POST requests exist
    if request.method == 'POST' and 'username' in request.json and 'password' in request.json and 'email' in request.json:
        username = request.json['username']
        password = request.json['password']
        email = request.json['email']

        cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
        cursor.execute('SELECT * FROM accounts WHERE username = %s', (username,))
        account = cursor.fetchone()

        if account:
            return jsonify({"error": "Account already exists"}), 409
        elif not re.match(r'[^@]+@[^@]+\.[^@]+', email):
            return jsonify({"error": "Invalid email address"}), 400
        elif not re.match(r'[A-Za-z0-9]+', username):
            return jsonify({"error": "Username must contain only characters and numbers"}), 400
        elif not username or not password or not email:
            return jsonify({"error": "Please fill out the form"}), 400
        else:
            # Hash the password
            hash = password + app.secret_key
            hash = hashlib.sha1(hash.encode())
            password = hash.hexdigest()

            # Insert the new account
            cursor.execute('INSERT INTO accounts VALUES (NULL, %s, %s, %s)', (username, password, email,))
            mysql.connection.commit()
            return jsonify({"message": "Registration successful"}), 201
    return jsonify({"error": "Invalid request"}), 400

@app.route('/profile', methods=['GET'])
@login_required
def profile():
    cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
    cursor.execute('SELECT * FROM accounts WHERE id = %s', (current_user.id,))
    account = cursor.fetchone()
    return jsonify({"account": account}), 200

@app.route('/home', methods=['GET'])
@login_required
def home():
    session_id = current_user.id

    cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
    cursor.execute('SELECT time, jump_force FROM data WHERE userID = %s', (session_id,))
    data = cursor.fetchall()

    return jsonify({"data": data}), 200

@app.route('/trigger', methods=['POST'])
@login_required
def trigger_recording():
    requester_id = current_user.id

    # Get command payload
    data = request.get_json()
    authentication_token = data.get('authentication_token')
    command = data.get('command')
    user_id = current_user.id
    device_id = data.get('device_id')
    recording_name = data.get('recording_name')

    # Check for empty fields
    if not authentication_token or not command or not user_id or not device_id:
        return jsonify({"error": "All fields are required."}), 400

    # Handle recording_name logic
    cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
    if not recording_name:
        recording_name = "recording"

    # Check if the recording_name already exists and increment if necessary
    cursor.execute("SELECT COUNT(*) AS count FROM recordings WHERE name LIKE %s", (recording_name + '%',))
    result = cursor.fetchone()
    if result['count'] > 0:
        recording_name = f"{recording_name}-{result['count']}"

    # Insert command into commands table
    cursor.execute("INSERT INTO commands (device_id, user_id, command, authentication_token, recording_name) VALUES (%s, %s, %s, %s, %s)",
                   (device_id, user_id, command, authentication_token, recording_name))
    mysql.connection.commit()

    return jsonify({"message": "Command accepted", "recording_name": recording_name}), 200
    
# Store the last ping time for each ESP32
last_seen = {}

# Time (in seconds) after which an ESP32 is considered disconnected
TIMEOUT = 5

@app.route('/heartbeat', methods=['POST'])
def heartbeat():
    data = request.get_json()
    device_id = data.get('device_id')
    if not device_id:
        return jsonify({'error': 'Missing device_id'}), 400

    # Update last seen time
    last_seen[device_id] = time.time()

    # Prepare database cursor
    cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)

    # Collect start/stop commands
    response_data = {'status': 'OK', 'commands': []}

    for command_type in ['start_recording', 'stop_recording']:
        cursor.execute("SELECT * FROM commands WHERE device_id = %s AND command = %s", (device_id, command_type))
        commands = cursor.fetchall()

        if commands:
            response_data['commands'].extend(commands)
            cursor.execute("DELETE FROM commands WHERE device_id = %s AND command = %s", (device_id, command_type))
            mysql.connection.commit()

    return jsonify(response_data), 200


@app.route('/status/<device_id>', methods=['GET'])
def status(device_id):
    now = time.time()
    last = last_seen.get(device_id)
    if last and (now - last) < TIMEOUT:
        return jsonify({'device_id': device_id, 'connected': True})
    else:
        return jsonify({'device_id': device_id, 'connected': False})

'''
@app.route('/debug-session', methods=['GET'])
def debug_session():
    print("Debugging session state:")
    print(f"loggedin: {session.get('loggedin')}")
    print(f"id: {session.get('id')}")
    print(f"username: {session.get('username')}")
    return jsonify({
        "loggedin": session.get('loggedin'),
        "id": session.get('id'),
        "username": session.get('username')
    }), 200
'''
if __name__ == '__main__':
    cert_file = 'cert.pem'
    key_file = 'key.pem'

    if os.path.exists(cert_file) and os.path.exists(key_file):
        try:
            ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ssl_context.load_cert_chain(cert_file, key_file)
            print("SSL certificates loaded successfully.")
            app.run(host='0.0.0.0', port=5000, ssl_context=(cert_file, key_file))
        except Exception as e:
            print(f"Error loading SSL certificates: {e}")
            print("Falling back to HTTP...")
            app.run(host='0.0.0.0', port=5000)
    else:
        print("SSL certificates not found. Please generate 'cert.pem' and 'key.pem' for HTTPS.")
        print("Falling back to HTTP...")
        app.run(host='0.0.0.0', port=5000)
