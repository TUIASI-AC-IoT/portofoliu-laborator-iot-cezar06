from flask import Flask, jsonify, request
from flasgger import Swagger
import os
import uuid

app = Flask(__name__)

swagger_config = {
    "headers": [],
    "specs": [
        {
            "endpoint": "apispec",
            "route": "/apispec.json",
            "rule_filter": lambda rule: True,
            "model_filter": lambda tag: True,
        }
    ],
    "static_url_path": "/flasgger_static",
    "swagger_ui": True,
    "specs_route": "/docs",
}

swagger_template = {
    "swagger": "2.0",
    "info": {
        "title": "File Manager API",
        "description": "A RESTful API for managing files in a directory",
        "version": "1.0.0",
        "contact": {"email": "your-email@example.com"},
    },
    "basePath": "/",
    "schemes": ["http"],
    "consumes": ["application/json"],
    "produces": ["application/json"],
}

swagger = Swagger(app, config=swagger_config, template=swagger_template)

BASE_DIR = os.path.abspath(os.path.dirname(__file__))
MANAGED_FILES_DIR = os.path.join(BASE_DIR, "managed_files")

if not os.path.exists(MANAGED_FILES_DIR):
    os.makedirs(MANAGED_FILES_DIR)


@app.route("/")
def hello():
    """
    Root endpoint
    ---
    responses:
      200:
        description: A welcome message
        schema:
          type: string
    """
    return "File Manager API is running!"


@app.route("/files", methods=["GET"])
def list_files():
    """
    List all files in the managed directory
    ---
    responses:
      200:
        description: List of files in the directory
        schema:
          type: array
          items:
            type: string
      500:
        description: Server error
        schema:
          type: object
          properties:
            error:
              type: string
    """
    try:
        files = os.listdir(MANAGED_FILES_DIR)
        return jsonify(files), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/files/<string:filename>", methods=["GET"])
def get_file_content(filename):
    """
    Get the content of a specific file
    ---
    parameters:
      - name: filename
        in: path
        type: string
        required: true
        description: Name of the file to read
    responses:
      200:
        description: File content
        schema:
          type: object
          properties:
            filename:
              type: string
            content:
              type: string
      400:
        description: Invalid file type
        schema:
          type: object
          properties:
            error:
              type: string
      404:
        description: File not found
        schema:
          type: object
          properties:
            error:
              type: string
      500:
        description: Server error
        schema:
          type: object
          properties:
            error:
              type: string
    """
    file_path = os.path.join(MANAGED_FILES_DIR, filename)
    if not os.path.exists(file_path) or not os.path.isfile(file_path):
        return jsonify({"error": "File not found"}), 404
    try:
        if not filename.endswith(
            (".txt", ".md", ".log", ".py", ".json", ".xml", ".html", ".css", ".js")
        ):
            return (
                jsonify({"error": "File is not a text file or unsupported type"}),
                400,
            )

        with open(file_path, "r", encoding="utf-8") as f:
            content = f.read()
        return jsonify({"filename": filename, "content": content}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/files/<string:filename>", methods=["POST"])
def create_file_with_name(filename):
    """
    Create a new file with specified name and content
    ---
    parameters:
      - name: filename
        in: path
        type: string
        required: true
        description: Name of the file to create
    parameters:
      - name: body
        in: body
        required: true
        schema:
          type: object
          required:
            - content
          properties:
            content:
              type: string
              description: Content of the file
    responses:
      201:
        description: File created successfully
        schema:
          type: object
          properties:
            message:
              type: string
      400:
        description: Missing content in request body
        schema:
          type: object
          properties:
            error:
              type: string
      409:
        description: File already exists
        schema:
          type: object
          properties:
            error:
              type: string
      500:
        description: Server error
        schema:
          type: object
          properties:
            error:
              type: string
    """
    file_path = os.path.join(MANAGED_FILES_DIR, filename)
    if os.path.exists(file_path):
        return jsonify({"error": "File already exists"}), 409

    try:
        data = request.get_json()
        if data is None or "content" not in data:
            return jsonify({"error": "Missing content in request body"}), 400

        content = data["content"]
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(content)
        return jsonify({"message": f"File '{filename}' created successfully."}), 201
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/files", methods=["POST"])
def create_file_with_generated_name():
    """
    Create a new file with generated name and content
    ---
    parameters:
      - name: body
        in: body
        required: true
        schema:
          type: object
          required:
            - content
          properties:
            content:
              type: string
              description: Content of the file
    responses:
      201:
        description: File created successfully
        schema:
          type: object
          properties:
            message:
              type: string
            filename:
              type: string
      400:
        description: Missing content in request body
        schema:
          type: object
          properties:
            error:
              type: string
      500:
        description: Server error
        schema:
          type: object
          properties:
            error:
              type: string
    """
    try:
        data = request.get_json()
        if data is None or "content" not in data:
            return jsonify({"error": "Missing content in request body"}), 400

        content = data["content"]
        filename = str(uuid.uuid4()) + ".txt"
        file_path = os.path.join(MANAGED_FILES_DIR, filename)

        with open(file_path, "w", encoding="utf-8") as f:
            f.write(content)
        return (
            jsonify({"message": "File created successfully.", "filename": filename}),
            201,
        )
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/files/<string:filename>", methods=["DELETE"])
def delete_file_by_name(filename):
    """
    Delete a file by name
    ---
    parameters:
      - name: filename
        in: path
        type: string
        required: true
        description: Name of the file to delete
    responses:
      200:
        description: File deleted successfully
        schema:
          type: object
          properties:
            message:
              type: string
      404:
        description: File not found
        schema:
          type: object
          properties:
            error:
              type: string
      500:
        description: Server error
        schema:
          type: object
          properties:
            error:
              type: string
    """
    file_path = os.path.join(MANAGED_FILES_DIR, filename)
    if not os.path.exists(file_path) or not os.path.isfile(file_path):
        return jsonify({"error": "File not found"}), 404

    try:
        os.remove(file_path)
        return jsonify({"message": f"File '{filename}' deleted successfully."}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/files/<string:filename>", methods=["PUT"])
def modify_file_content(filename):
    """
    Modify the content of an existing file
    ---
    parameters:
      - name: filename
        in: path
        type: string
        required: true
        description: Name of the file to modify
    parameters:
      - name: body
        in: body
        required: true
        schema:
          type: object
          required:
            - content
          properties:
            content:
              type: string
              description: New content of the file
    responses:
      200:
        description: File modified successfully
        schema:
          type: object
          properties:
            message:
              type: string
      400:
        description: Missing content in request body
        schema:
          type: object
          properties:
            error:
              type: string
      404:
        description: File not found
        schema:
          type: object
          properties:
            error:
              type: string
      500:
        description: Server error
        schema:
          type: object
          properties:
            error:
              type: string
    """
    file_path = os.path.join(MANAGED_FILES_DIR, filename)
    if not os.path.exists(file_path) or not os.path.isfile(file_path):
        return jsonify({"error": "File not found"}), 404

    try:
        data = request.get_json()
        if data is None or "content" not in data:
            return jsonify({"error": "Missing content in request body"}), 400

        content = data["content"]
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(content)
        return jsonify({"message": f"File '{filename}' modified successfully."}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5001, debug=True)
