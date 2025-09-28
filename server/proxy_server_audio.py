from flask import Flask, request, jsonify, send_file
import requests
import time
import os
from datetime import datetime
from dotenv import load_dotenv
import random
import io
import logging
import sys

# Load environment variables from .env file
load_dotenv()

# Configure logging for Cloud Run
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    stream=sys.stdout
)
logger = logging.getLogger(__name__)

app = Flask(__name__)

ASSEMBLYAI_API_KEY = os.getenv("ASSEMBLYAI_API_KEY")
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")

# Child-friendly robot prompt
ROBOT_PROMPT = """The following is a message asked by a child under 12 within the context of a robotics activity. You are a robot created for entertainment purposes. Please answer this message in an entertaining, yet informative way that does not include any profanity and remains age-appropriate. Your response should be short enough to be able to read out within 7 seconds (around 30-40 words max). Be enthusiastic and fun!

Child's question: """

# Root route
@app.route('/')
def root():
    return jsonify({
        'status': 'AI Robot Server - Cloud Deployed',
        'endpoints': {
            'health': '/health',
            'upload': '/upload',
            'audio': '/audio/<filename>'
        }
    })

# Route to receive audio from ESP32
@app.route('/upload', methods=['POST'])
def upload():
    if 'audio' not in request.files:
        return jsonify({'error': 'No audio file uploaded'}), 400

    audio_file = request.files['audio']
    audio_data = audio_file.read()

    # Save locally for debugging
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    mic_input_audio_dir = "/tmp/mic_input" if os.path.exists("/tmp") else "mic_input"
    filename = f"{mic_input_audio_dir}/mic_input_{timestamp}.wav"
    
    # Create directory if it doesn't exist
    os.makedirs(mic_input_audio_dir, exist_ok=True)
    
    with open(filename, "wb") as f:
        f.write(audio_data)
    
    print(f"🎤 Audio saved: {filename}")

    # Step 1: Upload audio to AssemblyAI
    print("📄 Transcribing audio...")
    upload_response = requests.post(
        'https://api.assemblyai.com/v2/upload',
        headers={'authorization': ASSEMBLYAI_API_KEY},
        data=audio_data
    )
    
    if upload_response.status_code != 200:
        return jsonify({'error': 'Failed to upload audio'}), 500
    
    upload_url = upload_response.json()['upload_url']

    # Step 2: Request transcription
    transcript_response = requests.post(
        'https://api.assemblyai.com/v2/transcript',
        headers={'authorization': ASSEMBLYAI_API_KEY, 'content-type': 'application/json'},
        json={'audio_url': upload_url}
    )
    
    if transcript_response.status_code != 200:
        return jsonify({'error': 'Failed to request transcription'}), 500
    
    transcript_id = transcript_response.json()['id']

    # Step 3: Polling for transcription completion
    print(f"⏳ Waiting for transcription (ID: {transcript_id})...")
    max_attempts = 30  # 60 seconds max
    attempt = 0
    
    while attempt < max_attempts:
        polling_response = requests.get(
            f'https://api.assemblyai.com/v2/transcript/{transcript_id}',
            headers={'authorization': ASSEMBLYAI_API_KEY}
        )
        
        if polling_response.status_code != 200:
            return jsonify({'error': 'Failed to check transcription status'}), 500
        
        result = polling_response.json()
        
        if result['status'] == 'completed':
            transcript_text = result['text']
            print(f"✅ Transcription: '{transcript_text}'")
            
            # Step 4: Send to ChatGPT for child-friendly response
            if not transcript_text or transcript_text.strip() == "":
                return jsonify({
                    'text': transcript_text or "[No speech detected]",
                    'ai_response': "I didn't hear anything! Try speaking a bit louder next time!",
                    'has_audio': False
                })
            
            try:
                print("🤖 Generating AI response...")
                chat_response = get_ai_response(transcript_text)
                print(f"🎭 AI Response: '{chat_response}'")
                
                # Step 5: Convert AI response to speech (FIXED SAMPLE RATE)
                print("🔊 Converting to speech...")
                audio_file_path = text_to_speech(chat_response, timestamp)
                
                if audio_file_path:
                    return jsonify({
                        'text': transcript_text,
                        'ai_response': chat_response,
                        'has_audio': True,
                        'audio_url': f'/audio/{timestamp}.wav'
                    })
                else:
                    return jsonify({
                        'text': transcript_text,
                        'ai_response': chat_response,
                        'has_audio': False
                    })
                
            except Exception as e:
                print(f"❌ AI Response Error: {e}")
                return jsonify({
                    'text': transcript_text,
                    'ai_response': "Oops! My robot brain is having trouble right now. Try asking me again!",
                    'has_audio': False
                })
            
        elif result['status'] == 'failed':
            return jsonify({'error': result['error']}), 500
        
        time.sleep(2)
        attempt += 1
    
    return jsonify({'error': 'Transcription timeout'}), 500

def get_ai_response(user_question):
    """Get child-friendly response from OpenAI"""
    if not OPENAI_API_KEY:
        return "Sorry! I need my AI brain to be connected to answer questions."
    
    try:
        response = requests.post(
            'https://api.openai.com/v1/chat/completions',
            headers={
                'Authorization': f'Bearer {OPENAI_API_KEY}',
                'Content-Type': 'application/json'
            },
            json={
                'model': 'gpt-3.5-turbo',
                'messages': [
                    {
                        'role': 'system',
                        'content': ROBOT_PROMPT
                    },
                    {
                        'role': 'user', 
                        'content': user_question
                    }
                ],
                'max_tokens': 80,
                'temperature': 0.8
            },
            timeout=10
        )
        
        if response.status_code == 200:
            ai_text = response.json()['choices'][0]['message']['content'].strip()
            return ai_text
        else:
            print(f"OpenAI API Error: {response.status_code} - {response.text}")
            return "My robot circuits are a bit confused right now! Ask me something else!"
            
    except requests.exceptions.Timeout:
        return "I'm thinking too hard! Ask me again!"
    except Exception as e:
        print(f"AI Error: {e}")
        return "Beep boop! Something went wrong in my robot brain!"

def text_to_speech(text, timestamp):
    """Convert text to speech using OpenAI TTS - FIXED FOR ESP32"""
    if not OPENAI_API_KEY:
        return None
        
    try:
        # Request TTS with 16kHz to match ESP32 setup
        response = requests.post(
            'https://api.openai.com/v1/audio/speech',
            headers={
                'Authorization': f'Bearer {OPENAI_API_KEY}',
                'Content-Type': 'application/json'
            },
            json={
                'model': 'tts-1',
                'input': text,
                'voice': 'nova',  # Child-friendly voice
                'response_format': 'wav',
                'speed': 0.9  # Slightly slower for kids
                # Note: OpenAI TTS outputs at 24kHz by default
            },
            timeout=30
        )
        
        if response.status_code == 200:
            # Create audio directory if it doesn't exist
            audio_dir = "/tmp/audio_responses" if os.path.exists("/tmp") else "audio_responses"
            
            # Save the original audio file first
            original_filename = f"{audio_dir}/{timestamp}_original.wav"
            with open(original_filename, 'wb') as f:
                f.write(response.content)
            
            print(f"🔊 Original TTS audio saved: {original_filename}")
            
            # Convert to 16kHz using ffmpeg (if available) or save as-is
            final_filename = f"{audio_dir}/{timestamp}.wav"
            
            # Try to convert sample rate using ffmpeg
            import subprocess
            try:
                # Convert 24kHz to 16kHz using ffmpeg
                cmd = [
                    'ffmpeg', '-y', '-i', original_filename,
                    '-ar', '16000',  # Set to 16kHz
                    '-ac', '1',      # Mono
                    '-sample_fmt', 's16',  # 16-bit
                    final_filename
                ]
                result = subprocess.run(cmd, capture_output=True, text=True)
                
                if result.returncode == 0:
                    print(f"✅ Converted to 16kHz: {final_filename}")
                    # Remove original to save space
                    os.remove(original_filename)
                    return final_filename
                else:
                    print(f"⚠️ FFmpeg conversion failed: {result.stderr}")
                    # Fall back to original file
                    os.rename(original_filename, final_filename)
                    print("⚠️ Using original TTS file (may have sample rate mismatch)")
                    return final_filename
                    
            except FileNotFoundError:
                print("⚠️ FFmpeg not found - using original TTS audio")
                print("⚠️ Install ffmpeg for proper sample rate conversion")
                os.rename(original_filename, final_filename)
                return final_filename
            
        else:
            print(f"OpenAI TTS Error: {response.status_code} - {response.text}")
            return None
            
    except Exception as e:
        print(f"TTS Error: {e}")
        return None

# Route to serve audio files
@app.route('/audio/<filename>')
def serve_audio(filename):
    try:
        file_path = f"audio_responses/{filename}"
        print(f"🎵 Serving audio file: {file_path}")
        
        # Check if file exists
        if not os.path.exists(file_path):
            print(f"❌ Audio file not found: {file_path}")
            return jsonify({'error': 'Audio file not found'}), 404
        
        # Log file info
        file_size = os.path.getsize(file_path)
        print(f"📁 Serving file size: {file_size} bytes")
        
        # Always serve as WAV with correct headers
        return send_file(file_path, mimetype='audio/wav')
        
    except Exception as e:
        print(f"❌ Error serving audio: {e}")
        return jsonify({'error': 'Audio file not found'}), 404

# Health check endpoint
@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'Robot server is running! 🤖', 'env': 'cloud'})

if __name__ == '__main__':
    # Check if API keys are set
    if not ASSEMBLYAI_API_KEY:
        print("⚠️  WARNING: ASSEMBLYAI_API_KEY environment variable not set!")
    if not OPENAI_API_KEY:
        print("⚠️  WARNING: OPENAI_API_KEY environment variable not set!")
    
    print("🤖 Starting AI Robot Server with TTS...")
    print("🎤 Ready to receive audio and respond with properly formatted speech!")
    
    # Check for ffmpeg
    import subprocess
    try:
        subprocess.run(['ffmpeg', '-version'], capture_output=True)
        print("✅ FFmpeg found - audio conversion will work properly")
    except FileNotFoundError:
        print("⚠️ FFmpeg not found - install it for best audio quality")
        print("   Ubuntu/Debian: sudo apt install ffmpeg")
        print("   macOS: brew install ffmpeg")
        print("   Windows: Download from https://ffmpeg.org/")
    
    port = int(os.environ.get('PORT', 5050))
    app.run(host='0.0.0.0', port=port, debug=False)