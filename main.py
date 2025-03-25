from flask import Flask, render_template, request, redirect, url_for, session, jsonify
from flask_mysqldb import MySQL
import MySQLdb.cursors, re, hashlib
import os

app = Flask(__name__)

# Load configuration from config.py if it exists, otherwise use default values
if os.path.exists('config.py'):
    app.config.from_pyfile('config.py')
else:
    app.secret_key = 'your secret key'
    app.config['MYSQL_HOST'] = 'localhost'
    app.config['MYSQL_USER'] = 'root'
    app.config['MYSQL_PASSWORD'] = 'root'
    app.config['MYSQL_DB'] = 'pythonlogin'

# Intialize MySQL
mysql = MySQL(app)

# http://localhost:5000/ - the following will be our login page, which will use both GET and POST requests
@app.route('/', methods=['GET', 'POST'])
def login():
    msg = ''
    # Check if "username" and "password" POST requests exist (user submitted form)
    if request.method == 'POST' and 'username' in request.form and 'password' in request.form:
        # Create variables for easy access
        username = request.form['username']
        password = request.form['password']
        
        # Hash the entered password in the same way as you did when registering
        hash = password + app.secret_key
        hash = hashlib.sha1(hash.encode())
        hashed_password = hash.hexdigest()

        # Check if account exists using MySQL
        cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
        cursor.execute('SELECT * FROM accounts WHERE username = %s AND password = %s', (username, hashed_password,))
        account = cursor.fetchone()

        # If account exists, log the user in
        if account:
            session['loggedin'] = True
            session['id'] = account['id']
            session['username'] = account['username']
            return redirect(url_for('home'))
        else:
            msg = 'Incorrect username/password!'
    
    return render_template('index.html', msg=msg)


# http://localhost:5000/logout - this will be the logout page
@app.route('/logout')
def logout():
    # Remove session data, this will log the user out
   session.pop('loggedin', None)
   session.pop('id', None)
   session.pop('username', None)
   # Redirect to login page
   return redirect(url_for('login'))

# http://localhost:5000/register - this will be the registration page, we need to use both GET and POST requests
@app.route('/register', methods=['GET', 'POST'])
def register():
    # Output message if something goes wrong...
    msg = ''
    # Check if "username", "password" and "email" POST requests exist (user submitted form)
    if request.method == 'POST' and 'username' in request.form and 'password' in request.form and 'email' in request.form:
        # Create variables for easy access
        username = request.form['username']
        password = request.form['password']
        email = request.form['email']
        # Check if account exists using MySQL
        cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
        cursor.execute('SELECT * FROM accounts WHERE username = %s', (username,))
        account = cursor.fetchone()
        # If account exists show error and validation checks
        if account:
            msg = 'Account already exists!'
        elif not re.match(r'[^@]+@[^@]+\.[^@]+', email):
            msg = 'Invalid email address!'
        elif not re.match(r'[A-Za-z0-9]+', username):
            msg = 'Username must contain only characters and numbers!'
        elif not username or not password or not email:
            msg = 'Please fill out the form!'
        else:
            # Hash the password
            hash = password + app.secret_key
            hash = hashlib.sha1(hash.encode())
            password = hash.hexdigest()
            # Account doesn't exist, and the form data is valid, so insert the new account into the accounts table
            cursor.execute('INSERT INTO accounts VALUES (NULL, %s, %s, %s)', (username, password, email,))
            mysql.connection.commit()
            msg = 'You have successfully registered!'
    elif request.method == 'POST':
        # Form is empty... (no POST data)
        msg = 'Please fill out the form!'
    # Show registration form with message (if any)
    return render_template('register.html', msg=msg)

# http://localhost:5000/profile - this will be the profile page, only accessible for logged in users
@app.route('/profile')
def profile():
    # Check if the user is logged in
    if 'loggedin' in session:
        # We need all the account info for the user so we can display it on the profile page
        cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
        cursor.execute('SELECT * FROM accounts WHERE id = %s', (session['id'],))
        account = cursor.fetchone()
        # Show the profile page with account info
        return render_template('profile.html', account=account)
    # User is not logged in redirect to login page
    return redirect(url_for('login'))

# http://localhost:5000/home - this will be the home page, only accessible for logged in users
@app.route('/home')
def home():
    # Check if the user is logged in
    if 'loggedin' in session:
        cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
        cursor.execute('SELECT time, jump_force FROM data WHERE userID = %s', (session['id'],))
        data = cursor.fetchall()
        
        # Extract time and jump_force into separate lists
        time_data = [float(row['time']) for row in data]
        force_data = [float(row['jump_force']) for row in data]
        
        # User is loggedin show them the home page
        return render_template('home.html', username=session['username'], time_data=time_data, force_data=force_data)
    # User is not loggedin redirect to login page
    return redirect(url_for('login'))

@app.route('/trigger', methods=['POST'])
def trigger_recording():
    msg = ''
    # Get and verify token
    auth_header = request.headers.get('Authorization')
    if not auth_header:
        return jsonify({"error": "Missing Authorization"}), 401
    
    token = auth_header.split(" ")[1]
    cursor = mysql.connection.cursor(MySQLdb.cursors.DictCursor)
    cursor.execute("SELECT * FROM clients WHERE api_token = %s", (token,))
    client = cursor.fetchone()
    
    if not client:
        return jsonify({"error": "Unauthorized"}), 401
    
    client_id = client['id']

    # Get command payload
    data = request.get_json()
    device_id = data.get('device_id')
    command = data.get('command')
    user_id = data.get('user_id')

    # Verify this client owns the ESP32
    cursor.execute("SELECT * FROM devices WHERE device_id = %s AND client_id = %s", (device_id, client_id))
    device = cursor.fetchone()

    if not device:
        return jsonify({"error": "Forbidden: You do not own this device"}), 403

    # Insert command into commands table
    cursor.execute("INSERT INTO commands (device_id, user_id, command) VALUES (%s, %s, %s)", (device_id, user_id, command))
    mysql.connection.commit()

    return jsonify({"message": "Command accepted"}), 200
