from flask import Flask, request, jsonify
import requests
import time
import os

from datetime import datetime

app = Flask(__name__)

ASSEMBLYAI_API_KEY = os.environ.get("ASSEMBLYAI_API_KEY")  # Set this in your environment

# Route to receive audio from ESP32
@app.route('/upload', methods=['POST'])
def upload():
    if 'audio' not in request.files:
        return jsonify({'error': 'No audio file uploaded'}), 400

    audio_file = request.files['audio']
    audio_data = audio_file.read()

    # Save locally for debugging
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"debug_audio_{timestamp}.wav"
    with open(filename, "wb") as f:
        f.write(audio_data)


    # Upload audio to AssemblyAI
    upload_response = requests.post(
        'https://api.assemblyai.com/v2/upload',
        headers={'authorization': ASSEMBLYAI_API_KEY},
        data=audio_data
    )
    upload_url = upload_response.json()['upload_url']

    # Request transcription
    transcript_response = requests.post(
        'https://api.assemblyai.com/v2/transcript',
        headers={'authorization': ASSEMBLYAI_API_KEY, 'content-type': 'application/json'},
        json={'audio_url': upload_url}
    )
    transcript_id = transcript_response.json()['id']

    # Polling for transcription completion
    while True:
        polling_response = requests.get(
            f'https://api.assemblyai.com/v2/transcript/{transcript_id}',
            headers={'authorization': ASSEMBLYAI_API_KEY}
        )
        result = polling_response.json()
        if result['status'] == 'completed':
            return jsonify({'text': result['text']})
        elif result['status'] == 'failed':
            return jsonify({'error': result['error']}), 500
        time.sleep(2)


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5050)

