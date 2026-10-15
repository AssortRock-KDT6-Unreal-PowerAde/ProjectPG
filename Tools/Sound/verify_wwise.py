import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from waapi import call
r = call('ak.wwise.core.object.get', {'waql': r'$ "\Containers" select descendants where type != "AudioFileSource"'},
         {'return': ['path', 'type', '@Volume', '@EnableAttenuation', '@3DSpatialization', '@Attenuation']})['return']
for o in r:
    if any(k in o['path'] for k in ('Weapon', 'Player', 'World')):
        print(o['type'], o['path'][12:], o.get('@Volume'), o.get('@EnableAttenuation'), o.get('@3DSpatialization'), o.get('@Attenuation', {}).get('name'))
print([o['name'] for o in call('ak.wwise.core.object.get', {'waql': '$ from type Event'}, {'return': ['name']})['return']])
for b in ['Weapon', 'Player', 'World']:
    print(b, len(call('ak.wwise.core.soundbank.getInclusions', {'soundbank': r'\SoundBanks\Default Work Unit' + '\\' + b})['inclusions']))
