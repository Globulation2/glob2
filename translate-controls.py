from pathlib import Path
import json
translations={
'ar':['المراعي','التلال الصخرية','دروب الحيوانات','كمية الأسماك','عدد المخيمات'],
'br':['Pradaria','Colinas rochosas','Trilhas de animais','Quantidade de peixes','Quantidade de acampamentos'],
'ca':['Praderia','Turons rocosos','Corriols d’animals','Quantitat de peixos','Nombre de campaments'],
'cz':['Louky','Skalnaté pahorky','Zvířecí stezky','Množství ryb','Počet tábořišť'],
'de':['Grasland','Felshügel','Wildwechsel','Fischmenge','Anzahl der Lagerplätze'],
'dk':['Græsland','Klippebakker','Dyrestier','Fiskemængde','Antal lejrpladser'],
'es':['Pradera','Colinas rocosas','Senderos de animales','Cantidad de peces','Número de campamentos'],
'eo':['Herbejo','Rokmontetoj','Bestaj padoj','Kvanto da fiŝoj','Nombro de kampadejoj'],
'eu':['Belardia','Harkaitz-muinoak','Animalien bidexkak','Arrain kopurua','Kanpaleku kopurua'],
'fa':['علفزار','تپه‌های سنگی','مسیر جانوران','مقدار ماهی','تعداد اردوگاه‌ها'],
'fr':['Prairie','Collines rocheuses','Pistes d’animaux','Quantité de poissons','Nombre de campements'],
'gr':['Λιβάδια','Βραχώδεις λόφοι','Μονοπάτια ζώων','Ποσότητα ψαριών','Αριθμός καταυλισμών'],
'hu':['Füves terület','Sziklás dombok','Vadcsapások','Halak mennyisége','Táborhelyek száma'],
'it':['Prateria','Colline rocciose','Sentieri degli animali','Quantità di pesci','Numero di accampamenti'],
'nl':['Grasland','Rotsheuvels','Wildpaden','Hoeveelheid vis','Aantal kampeerplaatsen'],
'pl':['Łąki','Skaliste wzgórza','Ścieżki zwierząt','Ilość ryb','Liczba obozowisk'],
'pt':['Pradaria','Colinas rochosas','Trilhos de animais','Quantidade de peixes','Número de acampamentos'],
'ro':['Pajiști','Dealuri stâncoase','Poteci de animale','Cantitate de pește','Număr de tabere'],
'ru':['Луга','Скалистые холмы','Звериные тропы','Количество рыбы','Количество стоянок'],
'si':['Travišča','Skalnati griči','Živalske steze','Količina rib','Število taborišč'],
'sk':['Lúky','Skalnaté pahorky','Zvieracie chodníky','Množstvo rýb','Počet táborísk'],
'sr':['Травњаци','Каменита брда','Животињске стазе','Количина рибе','Број кампова'],
'fi':['Ruohomaa','Kalliokukkulat','Eläinpolut','Kalojen määrä','Leiripaikkojen määrä'],
'sv':['Gräsmark','Klippkullar','Djurstigar','Mängd fisk','Antal lägerplatser'],
'tr':['Otlak','Kayalık tepeler','Hayvan patikaları','Balık miktarı','Kamp yeri sayısı'],
'zh-tw':['草原','岩丘','獸徑','魚類數量','營地數量'],
'zh-cn':['草原','岩丘','兽径','鱼类数量','营地数量'],
'uk':['Луки','Скелясті пагорби','Звірині стежки','Кількість риби','Кількість стоянок'],
'id':['Padang rumput','Bukit berbatu','Jalur satwa','Jumlah ikan','Jumlah perkemahan'],
'vi':['Đồng cỏ','Đồi đá','Đường mòn thú rừng','Lượng cá','Số khu cắm trại'],
'ja':['草原','岩山','獣道','魚の量','野営地の数'],
'ko':['초원','바위 언덕','야생 동물 길','물고기 양','야영지 수'],
}
keys=['Grassland','Kopjes','Game trails','Fish amount','Camp-site amount']
for lang,values in translations.items():
 p=Path(f'data/texts.{lang}.txt');text=p.read_text()
 for key,value in zip(keys,values,strict=True):
  old=f'[{key}]\n{key}\n';assert text.count(old)==1,(lang,key)
  text=text.replace(old,f'[{key}]\n{value}\n')
 p.write_text(text)
p=Path('test/translation_shared_values.json');data=json.loads(p.read_text());assert 'Silence' not in data['shared_vocabulary']['texts.fr.txt'];data['shared_vocabulary']['texts.fr.txt'].append('Silence');data['_comment']+=' French Silence is the native spelling of the audio silence label; it is reviewed shared vocabulary, not an untranslated sentence.'
p.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
