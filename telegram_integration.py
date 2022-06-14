#!/usr/bin/python3

import pytz
from datetime import datetime
from mkpy.utility import *
import file_utility as fu

# Uses Pyrogram (https://docs.pyrogram.org/) as the Telegram library
from pyrogram import Client
import piexif

def download_photos():
    """
    This downloads all media groups assuming they are all photos. It assigns a
    single identifier to each photo album and sequentialy number pictures in
    it.

    To specify where to stop, it looks for a message with START as content,
    which should be a reply to the first message that should not be included in
    the full download.
    """
    api_id=00000000
    api_hash='xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'

    path = os.path.abspath(path_resolve("~/.weaver/files/book-photos/"))

    final_message_id = None

    group_id_to_messages = {}
    with Client("my_account", api_id, api_hash) as app:
        for message in app.get_chat_history("me"):
            if message.id == final_message_id:
                break

            if message.media != None:
                if message.media_group_id not in group_id_to_messages.keys():
                    group_id_to_messages[message.media_group_id] = []
                group_id_to_messages[message.media_group_id].append(message)

                # TODO: Whenever there's a picture that's a reply to another
                # one, assume it's in the same media group. This is useful for
                # cases where the user forgets a picture, before submitting the
                # album. Users can still "add into the album", even though
                # Telegram doesn't allow this.

            elif message.text == "START":
                final_message_id = message.reply_to_message_id;

        for messages in group_id_to_messages.values():
            identifier = fu.new_identifier()
            for i, message in enumerate(reversed(messages), 1):
                _, new_fname = fu.new_canonical_name(identifier=identifier, location=[i], extension="jpg")
                fpath = path_cat(path, new_fname)
                app.download_media(message, fpath)

                # Images returned by Telegram don't have any Exif data. We
                # embed the message's timestamp into it.
                
                # Even though Telegram stores message timestamps as in UNIX
                # epoch format, pyrogram translates that using the system's
                # timezone but returns a timezone unaware object.

                # Use the message's datetime to set the EXIF timestamp which is
                # usually the local time. 
                exif_dict = piexif.load(fpath)

                timezone_aware_date = message.date.replace(tzinfo=datetime.utcnow().astimezone().tzinfo)
                exif_dict["0th"][piexif.ImageIFD.DateTime] = timezone_aware_date.strftime("%Y:%m:%d %H:%M:%S")
                no_colon = timezone_aware_date.strftime("%z")
                exif_dict["Exif"][piexif.ExifIFD.OffsetTime] = no_colon[:3] + ":" + no_colon[3:]

                exif_bytes = piexif.dump(exif_dict)
                piexif.insert(exif_bytes, fpath)

download_photos()

# Uses python-telegram (https://github.com/alexander-akhmetov/python-telegram)
#from telegram.client import Telegram
#
#import json 
#import string
#import datetime
#import pytz 
#from collections import Counter

#def dump_chat_messages_to_file (tg, chat_id, fname):
#    error = None
#    all_messages = []
#    receive_limit = 100
#    receive = True
#    from_message_id = 0
#    stats_data = {}
#
#    while receive:
#        response = tg.get_chat_history(
#            chat_id=chat_id,
#            limit=1000,
#            from_message_id=from_message_id,
#        )
#        response.wait()
#
#        if not response.error:
#            all_messages += response.update['messages']
#            from_message_id = all_messages[-1]['id']
#
#            if not response.update['total_count']:
#                receive = False
#
#        else:
#            error = response.error_info
#            break
#
#    if error == None:
#        print(f'Messages received: {len(all_messages)}')
#
#        all_messages.sort(key = lambda message: message['date'])
#
#        with open(fname, 'w') as json_file:
#          json.dump(all_messages, json_file, indent=2)
#
#    else:
#        print(f'error: {error}')
#
#def tg_get_user_id(tg):
#    response = tg.get_me()
#    response.wait()
#    return response.update['id']
#
#def slice_until_char(s, s_idx, chars):
#    result = ''
#
#    while True:
#        if s[s_idx] == '\\' and s[s_idx+1] in chars:
#            result += s[s_idx+1]
#            s_idx += 2
#        elif s[s_idx] not in chars:
#            result += s[s_idx]
#            s_idx += 1
#        else:
#            break
#
#    return result
#
#class SplxInstance():
#    def __init__(self, type_name):
#        self.type_name = type_name
#        self.parameters = []
#        self.floating = []
#
#def print_instance(instance, indent=0, end='\n'):
#    print(f'{" "*indent}{instance.type_name}', end='')
#
#    if len(instance.parameters) > 0:
#        string_parameters = [f'"{p}"' for p in instance.parameters]
#        print(f'[{" ".join(string_parameters)}]', end='')
#
#    if len(instance.floating) > 0:
#        print ('{')
#        print ('  items')
#        total = len(instance.floating)
#        for i, item in enumerate(instance.floating):
#            sep = ',\n'
#            if i == total-1:
#                sep = '\n'
#            
#            print_instance(item, indent=indent+4, end=sep)
#        print ('}', end='')
#    print(end=end)
#
#def print_message_data(messages):
#    #tz_str = None
#    tz_str = 'America/Bogota'
#    #tz_str = 'America/Mexico_City'
#
#    message_data = {}
#
#    for msg in messages:
#        if msg['content']['@type'] == 'messageText':
#            content = msg['content']['text']['text']
#            #print("SOURCE: " + content)
#
#            movement = None
#            content_idx = 0
#            while content_idx < len(content):
#                if content[content_idx] == '/':
#                    content_idx += 1
#
#                    type_start = content_idx
#                    while content_idx < len(content) and (content[content_idx] != '(' and content[content_idx] != '\n'):
#                        content_idx += 1
#                    new_instance = SplxInstance(content[type_start:content_idx])
#
#                    parameters = []
#                    if content_idx < len(content) and content[content_idx] == '(':
#                        while content_idx < len(content) and content[content_idx] != ')':
#                            content_idx += 1
#
#                            if content[content_idx] != '/':
#                                parameter = slice_until_char(content, content_idx, [',', ')'])
#                                new_instance.parameters.append(parameter.strip())
#                                content_idx += len(parameter)
#
#                            elif content[content_idx] != '"' or content[content_idx] != "'":
#                                # TODO: Implement quoted strings?...
#                                pass
#
#                            else:
#                                # TODO: Implement nested objects?... hasn't been necessary yet.
#                                pass
#
#                        # Skip over ')'
#                        if content_idx < len(content) and content[content_idx] == ')':
#                            content_idx += 1
#
#
#                    if msg['reply_to_message_id'] != 0 and msg['reply_to_message_id'] in message_data.keys():
#                        parent_msg = message_data[msg['reply_to_message_id']].floating.append(new_instance)
#                    elif msg['id'] not in message_data.keys():
#                        message_data[msg['id']] = new_instance
#
#                    # This is specific to Money Flow movement objects
#                    if new_instance.type_name == 'movement':
#                        if movement == None:
#                            movement = new_instance
#
#                        if tz_str != None:
#                            timezone = pytz.timezone(tz_str)
#                        else:
#                            timezone = datetime.datetime.now().astimezone().tzinfo
#
#                        new_instance.parameters.append(datetime.datetime.fromtimestamp(msg['date'], tz=timezone).strftime('%Y-%m-%d %H:%M:%S%z'))
#
#                    elif new_instance.type_name == 'Item' and movement != None:
#                        new_instance.type_name = 'item'
#                        movement.floating.append(new_instance)
#
#                else:
#                    break
#
#                while content_idx < len(content) and (content[content_idx].isspace() or content[content_idx] == '\n'):
#                    content_idx += 1
#
#    for instance in message_data.values():
#        print_instance(instance)
#        print()
#
#if __name__ == '__main__':
#    messages_fname = 'telegram_messages.json'
#
#    tg = Telegram(
#        api_id=18631948,
#        api_hash='df784d80d2a3cf205ad3d08d7b341670',
#        phone='+525537344510',
#        database_encryption_key='mySuperSecretkEy',
#    )
#
#    tg.login()
#
#    # Apparently we need to do this after a login to actually get chat
#    # messages. Even if we know the chat ID in advance.
#    response = tg.get_chats()
#    response.wait()
#
#    # Dump my "Saved Messages" chat
#    saved_messages_chat_id = tg_get_user_id(tg)
#    dump_chat_messages_to_file (tg, saved_messages_chat_id, messages_fname)
#
#    tg.stop()
#
#    with open(messages_fname, 'r') as json_file:
#        messages = json.load(json_file)
#
#    #print_message_data (messages)
#
