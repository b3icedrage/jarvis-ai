from flask import Flask, request, jsonify, send_from_directory
import json

app = Flask(__name__, static_folder='.')

# Serve the index.html
@app.route('/')
def index():
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
    app.run(host='0.0.0.0', port=5000)
