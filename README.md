# Python Login System

This is a simple login system built with Flask and MySQL. It allows users to register, log in, and view their profile. The profile page is only accessible to logged-in users.

## Features

- User registration
- User login
- User profile page (only accessible when logged in)
- Password hashing for security

## Prerequisites

- Python 3.x
- MySQL
- Flask
- Flask-MySQLdb

## Installation

1. **Clone the repository:**

    ```sh
    git clone https://github.com/yourusername/python_login.git
    cd python_login
    ```

2. **Create a virtual environment and activate it:**

    ```sh
    python3 -m venv venv
    source venv/bin/activate
    ```

3. **Install the required packages:**

    ```sh
    pip install Flask Flask-MySQLdb
    ```

4. **Set up the MySQL database:**

    - Open MySQL and create a new database:

        ```sql
        CREATE DATABASE pythonlogin;
        ```

    - Create the `accounts` table:

        ```sql
        USE pythonlogin;
        CREATE TABLE accounts (
            id INT AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(50) NOT NULL,
            password VARCHAR(255) NOT NULL,
            email VARCHAR(100) NOT NULL,
            weight FLOAT
        );
        ```

5. **Configure the database connection:**

    - Open [main.py](http://_vscodecontentref_/0) and update the database connection details:

        ```python
        app.config['MYSQL_HOST'] = 'localhost'
        app.config['MYSQL_USER'] = 'your_mysql_username'
        app.config['MYSQL_PASSWORD'] = 'your_mysql_password'
        app.config['MYSQL_DB'] = 'pythonlogin'
        ```

## Running the Application

1. **Start the Flask application:**

    ```sh
    python main.py
    ```

2. **Open your web browser and navigate to:**

    ```
    http://localhost:5000/pythonlogin/
    ```

## Usage

- **Register:** Create a new account by providing a username, password, email, and weight.
- **Login:** Log in with your username and password.
- **Profile:** View your profile information, including your weight, on the profile page.

## License

This project is licensed under the MIT License.
