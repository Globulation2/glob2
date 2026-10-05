from pathlib import Path
import subprocess
translations={
'ar':('معدل الإطارات المستهدف للرسم','بلا حد','حدّد معدل الرسم في التطبيق كله. تقلّل القيم المنخفضة عمل الرسم. لا تتغير سرعة اللعبة.'),
'br':('Niver pal a skeudennoù dre eilenn','Hep bevenn','Bevenniñ an tresañ en arload a-bezh. An talvoudoù izeloc’h a ziskar al labour tresañ. Ne cheñch ket tizh ar c’hoari.'),
'ca':('FPS de renderització objectiu','Sense límit','Limita el dibuix a tota l’aplicació. Els valors més baixos redueixen el treball de renderització. La velocitat del joc no canvia.'),
'cz':('Cílové FPS vykreslování','Bez omezení','Omezí vykreslování v celé aplikaci. Nižší hodnoty snižují nároky na vykreslování. Rychlost hry se nemění.'),
'de':('Zielbildrate für die Darstellung','Unbegrenzt','Begrenzt die Darstellung in der gesamten Anwendung. Niedrigere Werte reduzieren den Zeichenaufwand. Die Spielgeschwindigkeit bleibt unverändert.'),
'dk':('Ønsket billedfrekvens','Ubegrænset','Begræns tegningen i hele programmet. Lavere værdier reducerer tegningsarbejdet. Spillets hastighed er uændret.'),
'es':('FPS de renderizado objetivo','Sin límite','Limita el dibujo en toda la aplicación. Los valores más bajos reducen el trabajo de renderizado. La velocidad del juego no cambia.'),
'eo':('Cela bildfrekvenco','Senlima','Limigu desegnadon en la tuta aplikaĵo. Pli malaltaj valoroj reduktas la desegnan laboron. La ludrapido restas senŝanĝa.'),
'eu':('Helburuko errendatze-FPS','Mugarik gabe','Mugatu marrazketa aplikazio osoan. Balio baxuagoek errendatze-lana murrizten dute. Jokoaren abiadura ez da aldatzen.'),
'fa':('نرخ فریم هدف برای ترسیم','نامحدود','ترسیم را در سراسر برنامه محدود می‌کند. مقدارهای کمتر، کار ترسیم را کاهش می‌دهند. سرعت بازی تغییر نمی‌کند.'),
'fr':('Fréquence d’affichage cible','Illimitée','Limite le dessin dans toute l’application. Des valeurs plus faibles réduisent le travail de rendu. La vitesse du jeu reste inchangée.'),
'gr':('Στόχος καρέ ανά δευτερόλεπτο','Χωρίς όριο','Περιορίζει τη σχεδίαση σε όλη την εφαρμογή. Οι χαμηλότερες τιμές μειώνουν την εργασία απόδοσης. Η ταχύτητα του παιχνιδιού δεν αλλάζει.'),
'hu':('Célzott megjelenítési FPS','Korlátlan','Korlátozza a rajzolást az egész alkalmazásban. Az alacsonyabb értékek csökkentik a megjelenítés munkáját. A játék sebessége nem változik.'),
'it':('FPS di rendering desiderati','Senza limite','Limita il disegno in tutta l’applicazione. Valori inferiori riducono il lavoro di rendering. La velocità del gioco non cambia.'),
'nl':('Gewenste beeldfrequentie','Onbeperkt','Beperk het tekenen in de hele toepassing. Lagere waarden verminderen het tekenwerk. De spelsnelheid blijft gelijk.'),
'pl':('Docelowa liczba klatek na sekundę','Bez limitu','Ogranicza rysowanie w całej aplikacji. Niższe wartości zmniejszają nakład pracy na renderowanie. Szybkość gry pozostaje bez zmian.'),
'pt':('FPS de renderização pretendidos','Sem limite','Limita o desenho em toda a aplicação. Valores mais baixos reduzem o trabalho de renderização. A velocidade do jogo não muda.'),
'ro':('FPS țintă pentru randare','Fără limită','Limitează desenarea în întreaga aplicație. Valorile mai mici reduc efortul de randare. Viteza jocului nu se schimbă.'),
'ru':('Целевая частота кадров','Без ограничений','Ограничивает отрисовку во всём приложении. Меньшие значения снижают нагрузку на отрисовку. Скорость игры не меняется.'),
'si':('Ciljna hitrost izrisa sličic','Brez omejitve','Omeji izris v celotni aplikaciji. Nižje vrednosti zmanjšajo delo pri izrisu. Hitrost igre se ne spremeni.'),
'sk':('Cieľové FPS vykresľovania','Bez obmedzenia','Obmedzuje vykresľovanie v celej aplikácii. Nižšie hodnoty znižujú nároky na vykresľovanie. Rýchlosť hry sa nemení.'),
'sr':('Циљни број кадрова у секунди','Без ограничења','Ограничава исцртавање у целој апликацији. Ниже вредности смањују посао исцртавања. Брзина игре се не мења.'),
'fi':('Piirron tavoitekuvataajuus','Rajoittamaton','Rajoita piirtämistä koko sovelluksessa. Pienemmät arvot vähentävät piirron työtä. Pelin nopeus ei muutu.'),
'sv':('Önskad bildfrekvens','Obegränsad','Begränsa ritningen i hela programmet. Lägre värden minskar renderingsarbetet. Spelets hastighet är oförändrad.'),
 'tr':('Hedef çizim FPS değeri','Sınırsız','Uygulamanın tamamında çizimi sınırlar. Daha düşük değerler çizim işini azaltır. Oyun hızı değişmez.'),
'zh-tw':('目標繪製幀率','無限制','限制整個應用程式的繪製頻率。較低的數值可減少繪製工作量。遊戲速度不變。'),
'zh-cn':('目标绘制帧率','无限制','限制整个应用程序的绘制频率。较低的数值可减少绘制工作量。游戏速度不变。'),
'uk':('Цільова частота кадрів','Без обмежень','Обмежує малювання в усьому застосунку. Нижчі значення зменшують роботу з малювання. Швидкість гри не змінюється.'),
'id':('Target FPS rendering','Tanpa batas','Batasi penggambaran di seluruh aplikasi. Nilai yang lebih rendah mengurangi pekerjaan rendering. Kecepatan permainan tidak berubah.'),
'vi':('FPS kết xuất mục tiêu','Không giới hạn','Giới hạn việc vẽ trong toàn bộ ứng dụng. Giá trị thấp hơn làm giảm công việc kết xuất. Tốc độ trò chơi không thay đổi.'),
'ja':('目標描画フレームレート','無制限','アプリ全体の描画頻度を制限します。低い値にすると描画処理が減ります。ゲームの速度は変わりません。'),
'ko':('목표 렌더링 FPS','무제한','앱 전체의 그리기 빈도를 제한합니다. 값이 낮을수록 렌더링 작업이 줄어듭니다. 게임 속도는 바뀌지 않습니다.')}
keys=['[settings Target render FPS]','[settings Unlimited]','[settings Limit drawing across the application. Lower values reduce rendering work. Game speed is unchanged.]']
for language,values in translations.items():
 p=Path(f'data/texts.{language}.txt');s=subprocess.check_output(['git','show','HEAD:'+str(p)]).decode();assert all(k not in s for k in keys),p
 p.write_text(s+('' if s.endswith('\n') else '\n')+''.join(k+'\n'+v+'\n' for k,v in zip(keys,values)))
print('Completed',len(translations),'catalogs /',len(translations)*3,'missing values')
