from flask import Flask, send_from_directory
import os

app = Flask(__name__, static_folder='.')

# Serve the anti-scam educational demo
@app.route('/')
def index():
    return send_from_directory('.', 'scam-demo.html')

if __name__ == '__main__':
    # Bind to 0.0.0.0 and honor the PORT injected by the hosting platform
    port = int(os.environ.get('PORT', 5000))
    app.run(host='0.0.0.0', port=port)
