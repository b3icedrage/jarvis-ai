from flask import Flask, request, jsonify, send_from_directory
import json
import os

app = Flask(__name__, static_folder='.')

# Serve the anti-scam educational demo
@app.route('/')
def index():
    return send_from_directory('.', 'scam-demo.html')

# Keep the original JARVIS demo page available
@app.route('/jarvis-demo')
def jarvis_demo():
    return send_from_directory('.', 'index.html')

# Endpoint for asking JARVIS
@app.route('/ask', methods=['POST'])
def ask():
    data = request.get_json()
    question = data.get('question', '')
    # Dummy response logic – echo back
    response = f"You asked: {question}. I don't have an answer yet."
    return jsonify({'response': response})

if __name__ == '__main__':
    # Bind to 0.0.0.0 and honor the PORT injected by the hosting platform
    port = int(os.environ.get('PORT', 5000))
    app.run(host='0.0.0.0', port=port)
