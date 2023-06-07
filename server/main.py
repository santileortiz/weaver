from flask import Flask, redirect, request
import os
import json
from mkpy.utility import *


app = Flask(__name__)

home = os.path.abspath(path_resolve('~/.weaver'))
if 'WEAVERHOME' in os.environ.keys():
    home = os.path.abspath(os.environ['WEAVERHOME'])
    print (ecma_cyan('info:') + f" home changed to '{home}'")

# TODO: Ideally we would do this. For now we are using JSON instead of TSPLX
#nodes = weaver.config.filter("type", "link-node")
with open(path_cat(home, 'config.json')) as config_file:
    config = json.load(config_file)

nodes = {}
for e in config:
    if "@id" in e.keys():
        nodes[e["@id" ]] = e

# TODO: At some point in the past I had thought the location for the map file
# should've been ".weaver/data/link-node/map_Q976XFMWMW.json". Was that a
# better idea than this?. Is it an even better idea to just use the full weaver
# data dump?.
# :autolink_map_location
maps_dir = path_cat(home, "files/map/")
for filename in os.listdir(maps_dir):
    file_path = os.path.join(maps_dir, filename)

    with open(file_path) as file:
        data = json.load(file)

    key = os.path.splitext(filename)[0]  # Remove file extension from filename
    nodes[key]["map"] = data


@app.route('/autolink')
def autolink():
    name = request.args.get('q')
    node_id = request.args.get('n')
    if name and node_id:
        if node_id in nodes.keys():
            source_node = nodes[node_id]

            if node_id in nodes.keys() and "priority" in source_node.keys():
                priority_list = source_node["priority"]

                for target_node_id in priority_list:
                    if target_node_id in nodes:
                        target_node = nodes[target_node_id]

                        if "map" in target_node.keys():
                            lower_name = name.lower()
                            if lower_name in target_node["map"].keys():
                                return redirect(target_node["url"] + target_node["map"][lower_name])
                            else:
                                continue
                        else:
                            return redirect(target_node["url"] + name)

        return redirect(f"https://google.com/search?q={name}")

    else:
        if not name:
            return "Please provide a name"

        if not node_id:
            return "Please provide a node_id"

@app.route('/')
def hello():
    return "This is Weaver!"

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=9000, debug=False)
