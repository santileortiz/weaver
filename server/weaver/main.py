from flask import Flask, redirect, request

app = Flask(__name__)

page_name_to_id = {
  "Sample PKB" : "68W5X99W59",
}

node = ["XJ4W2QCX02"]

@app.route('/autolink')
def redirect_to_website():
    name = request.args.get('q')
    node_id = request.args.get('n')
    if name:
        if node_id in node and name in page_name_to_id.keys():
            target_url = f"http://localhost:8000?n={page_name_to_id[name]}"
        else:
            target_url = f"https://google.com/search?q={name}"
        return redirect(target_url)
    else:
        return "Please provide a name"

@app.route('/')
def hello():
    return "This is Weaver!"

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=9000, debug=False)
