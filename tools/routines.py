"""Her daily-routine questions and the answers she plays back, in Turkish.

Each question has a few ways of asking and 10 "yes" and 10 "no" answers; the toy cycles through them
so the same answer never comes twice in a row. {name} is the child's name from
~/.config/unicorn-pet/personal.json: the pack is private (fw/personal/, gitignored), so no child's
name is in this repo or in the public firmware. Built by `tools/voice.py --routines`.

SCHEDULE says when each question is asked (see README, "Her daily routine"): "am" = the hour after
wake-up, "pm" = from 90 to 30 minutes before bedtime, a time = that time; "school" = school days only,
"free" = weekends and holidays only, "all" = every day.
"""

UNCLEAR = [
    "Hmm, tam anlayamadım. Sonra yine sorarım.", "Sesini tam duyamadım, birazdan tekrar sorarım.",
    "Bir daha sonra konuşalım, olur mu?", "Anlayamadım ama sorun değil, sonra sorarım.",
    "Hmm, kulaklarım tam duymadı. Sonra yine sorarım.", "Peki, birazdan tekrar sorarım.",
    "Tam duyamadım, birazdan tekrar soracağım.", "Sorun değil, sonra yine konuşuruz.",
    "Hmm, biraz sonra tekrar sorayım.", "Peki, ben burada bekliyorum.",
]

QUESTIONS = {
    "TEETH_AM": dict(when="am", days="all", ask=[
        "{name}, dişlerini fırçaladın mı?", "{name}, dişler pırıl pırıl mı bakalım, fırçaladın mı?",
        "Günaydın dişleri! {name}, dişlerini fırçaladın mı?",
        "{name}, sabah dişlerini fırçaladın mı?", "Günaydın {name}! Dişlerini fırçalamayı unutmadın, değil mi?", "{name}, diş fırçan seni bekliyordu, fırçaladın mı?", "{name}, gülümse bakalım! Dişlerini fırçaladın mı?", "{name}, dişlerin tertemiz mi? Fırçaladın mı?", "{name}, sabah diş fırçalama tamam mı?", "{name}, mikroplar gitti mi? Dişlerini fırçaladın mı?"]),
    "TEETH_PM": dict(when="pm", days="all", ask=[
        "{name}, birazdan uyku vakti. Dişlerini fırçaladın mı?", "{name}, uyumadan önce dişlerini fırçaladın mı?",
        "Dişler de uykuya hazır mı {name}? Fırçaladın mı?",
        "{name}, uyku vakti yaklaşıyor. Dişler fırçalandı mı?", "{name}, gece dişlerini fırçaladın mı?", "{name}, yatmadan önce dişler tamam mı?", "{name}, dişlerin de iyi geceler demek istiyor. Fırçaladın mı?", "{name}, akşam dişlerini fırçalamayı unutmadın, değil mi?", "{name}, gülümse bakalım, dişler uykuya hazır mı?", "{name}, mikropları kovduk mu? Dişlerini fırçaladın mı?"], answers="TEETH_AM"),
    "DRESS": dict(when="am", days="all", ask=[
        "{name}, pijamalarını çıkarıp üstünü değiştirdin mi?", "{name}, giyindin mi bakalım?",
        "Pijama hâlâ üstünde mi {name}? Giyindin mi?",
        "{name}, pijamalarını çıkardın mı?", "{name}, giyinme zamanı! Giyindin mi?", "{name}, kıyafetlerini giydin mi?", "Günaydın {name}! Üstünü değiştirdin mi?", "{name}, pijamalar dinlenmeye gitti mi? Giyindin mi?", "{name}, giyindin mi bakalım, ne giydin?", "{name}, hazır mısın? Üstünü giydin mi?"]),
    "SCHOOL_CLOTHES": dict(when="am", days="school", ask=[
        "{name}, okul kıyafetlerini giydin mi?", "{name}, okula hazır mısın? Okul kıyafetlerin üstünde mi?",
        "Okul kıyafetleri giyildi mi {name}?",
        "{name}, okul kıyafetin üstünde mi?", "{name}, okula gitmeye hazır mısın? Giyindin mi?", "{name}, okul formanı giydin mi?", "{name}, okul kıyafetleri tamam mı?", "{name}, okul için giyindin mi bakalım?", "{name}, okul kıyafetlerini giymeyi unutmadın, değil mi?", "Okul zamanı {name}! Kıyafetlerini giydin mi?"]),
    "HAIR": dict(when="am", days="all", ask=[
        "{name}, saçlarını taradın mı?", "{name}, saçlar düzgün mü bakalım, taradın mı?",
        "Tarak nerede {name}? Saçlarını taradın mı?",
        "{name}, saçların taranmış mı?", "{name}, saçlarını taramayı unutmadın, değil mi?", "{name}, saçlar düzgün mü? Taradın mı?", "{name}, tarağı kullandın mı?", "{name}, saçlarını taradın mı bakalım?", "{name}, saçların parlıyor mu? Taradın mı?", "Günaydın {name}! Saçlarını taradın mı?"]),
    "VITAMIN": dict(when="am", days="all", ask=[
        "{name}, vitaminini içtin mi?", "{name}, bugün vitaminini aldın mı?", "Vitamin zamanı {name}! İçtin mi?",
        "{name}, vitaminini unutmadın, değil mi?", "{name}, bugünkü vitaminini içtin mi?", "{name}, vitamin içme zamanı geldi. İçtin mi?", "{name}, güçlenmek için vitaminini aldın mı?", "{name}, vitaminin tamam mı?", "{name}, vitaminini içtin mi bakalım?", "Günaydın {name}! Vitaminini içtin mi?"]),
    "BREAKFAST": dict(when="am", days="all", ask=[
        "{name}, kahvaltını ettin mi?", "{name}, karnını doyurdun mu, kahvaltı yaptın mı?",
        "Kahvaltı zamanı {name}! Yedin mi?",
        "{name}, kahvaltı yaptın mı?", "{name}, karnın doydu mu? Kahvaltını ettin mi?", "{name}, kahvaltını yemeyi unutmadın, değil mi?", "{name}, kahvaltı tamam mı?", "{name}, sabah kahvaltını ettin mi bakalım?", "{name}, güne kahvaltıyla başladın mı?", "Günaydın {name}! Kahvaltı yaptın mı?"]),
    "WATER": dict(when="15:30", days="free", ask=[
        "{name}, su içtin mi?", "{name}, bugün yeterince su içtin mi?", "Su molası {name}! Bir bardak içtin mi?",
        "{name}, bir bardak su içtin mi?", "{name}, su içmeyi unutmadın, değil mi?", "{name}, susadın mı? Su içtin mi?", "{name}, bugün su içtin mi bakalım?", "{name}, su molası verdin mi?", "{name}, birkaç yudum su içtin mi?", "{name}, su şişen nerede? Su içtin mi?"]),
    "DINNER": dict(when="18:00", days="all", ask=[
        "{name}, akşam yemeğini güzelce yedin mi?", "{name}, karnın doydu mu, yemeğini yedin mi?",
        "Akşam yemeği nasıldı {name}? Güzelce yedin mi?",
        "{name}, akşam yemeğini yedin mi?", "{name}, tabağını bitirdin mi?", "{name}, yemeğini güzelce yedin mi bakalım?", "{name}, karnın tok mu? Yemeğini yedin mi?", "{name}, akşam yemeği tamam mı?", "{name}, yemeğini afiyetle yedin mi?", "{name}, sofrada güzelce yedin mi?"]),
    "TOYS": dict(when="pm", days="all", ask=[
        "{name}, oyuncaklarını topladın mı?", "{name}, oyuncaklar evlerine döndü mü?",
        "Oyuncaklar uyumaya hazır mı {name}? Topladın mı?",
        "{name}, oyuncaklar toplandı mı?", "{name}, odanı topladın mı?", "{name}, oyuncakları yerlerine koydun mu?", "{name}, oyuncakları toplamayı unutmadın, değil mi?", "{name}, oyuncaklar uyumaya gitti mi?", "{name}, oyuncaklarını topladın mı bakalım?", "{name}, ortalık toplandı mı?"]),
    "TOILET": dict(when="pm-last", days="all", ask=[
        "{name}, yatmadan önce tuvalete gittin mi?", "{name}, uyumadan tuvalete uğradın mı?",
        "Uyku öncesi son durak {name}! Tuvalete gittin mi?",
        "{name}, tuvalete gittin mi?", "{name}, yatmadan önce tuvalet tamam mı?", "{name}, uyumadan önce tuvalete gitmeyi unutmadın, değil mi?", "{name}, tuvalete uğradın mı bakalım?", "{name}, uyku öncesi tuvalet molası verdin mi?", "{name}, yatağa gitmeden tuvalete gittin mi?", "{name}, son bir tuvalet molası? Gittin mi?"]),
}

YES = {
    "TEETH_AM": ["Aferin sana, pırıl pırıl!", "Harika! Gülüşün parlıyor.", "Çok güzel, dişlerin teşekkür ediyor.",
                 "Süpersin! Mikroplar kaçtı.", "Bravo! Tertemiz dişler.", "Oley! Dişlerin bembeyaz oldu.",
                 "Ne güzel! Seninle gurur duyuyorum.", "Aferin! Dişlerin mutlu şimdi.", "Harikasın! Ağzın mis gibi.",
                 "Çok iyi! Böyle devam."],
    "DRESS": ["Aferin, çok güzel giyinmişsin!", "Harika, hazırsın!", "Ne kadar şık olmuşsun!",
              "Bravo, kendi kendine giyindin!", "Süpersin, pijamalar dinlenmeye gitti.",
              "Çok güzel! Bugün harika bir gün olacak.", "Aferin sana, hızlı ve hazır!",
              "Oley! Giyinmeyi çok iyi biliyorsun.", "Çok güzel olmuşsun, bayıldım!", "Tebrikler, gün başlıyor!"],
    "SCHOOL_CLOTHES": ["Aferin, okula hazırsın!", "Harika, çok şık bir öğrencisin!", "Bravo! Okul seni bekliyor.",
                       "Süper, bugün okulda çok eğleneceksin.", "Çok güzel olmuşsun!", "Oley, hazırsın!",
                       "Aferin sana, ne kadar düzenlisin!", "Harikasın! Arkadaşların seni bekliyor.",
                       "Çok iyi, böyle devam!", "Tebrikler, okul kıyafetlerin sana çok yakışıyor."],
    "HAIR": ["Aferin, saçların çok güzel!", "Harika, prenses gibi olmuşsun!", "Ne güzel taranmışsın!",
             "Bravo, saçların parlıyor!", "Süper, çok düzgün olmuş!", "Oley! Saçların ipek gibi.",
             "Çok güzel olmuşsun, bayıldım!", "Aferin sana, saçların teşekkür ediyor.",
             "Harikasın! Bugün çok şıksın.", "Tebrikler, tertemiz ve düzgün!"],
    "VITAMIN": ["Aferin, çok güçlü olacaksın!", "Harika, vitaminler seni koruyor!", "Bravo! Bugün enerjin yerinde.",
                "Süper, çok akıllısın!", "Oley! Kocaman olacaksın.", "Aferin sana, unutmadın!",
                "Çok güzel, sağlıklı bir gün!", "Harikasın! Vitaminler iş başında.", "Tebrikler, çok iyi yaptın!",
                "Ne güzel! Kendine iyi bakıyorsun."],
    "BREAKFAST": ["Aferin, karnın tok!", "Harika, güne enerjiyle başlıyorsun!", "Afiyet olsun!",
                  "Bravo! Kahvaltı günün en güzel yemeği.", "Süper, çok güçlü olacaksın!", "Afiyet şeker olsun!",
                  "Aferin sana, kahvaltını unutmadın!", "Harikasın! Artık her şeye hazırsın.",
                  "Tebrikler, çok iyi yaptın!", "Ne güzel! Midecik mutlu."],
    "WATER": ["Aferin, çok sağlıklısın!", "Harika, susuz kalmadın!", "Bravo! Su çok iyi gelir.",
              "Süper, çiçekler gibi büyüyeceksin!", "Oley! Su içmek çok güzel.", "Aferin sana, unutmadın!",
              "Çok güzel, şıkır şıkır!", "Harikasın! Vücudun teşekkür ediyor.", "Tebrikler, çok iyi yaptın!",
              "Ne güzel! Su en güzel içecek."],
    "DINNER": ["Afiyet olsun!", "Aferin, tabağını bitirdin!", "Harika, karnın tok!", "Bravo! Çok güçlü olacaksın.",
               "Süper, afiyet şeker olsun!", "Oley! Mideciğin mutlu.", "Aferin sana, ne güzel yemişsin!",
               "Harikasın! Yemeği yapan da çok sevinmiştir.", "Tebrikler, çok iyi yaptın!",
               "Ne güzel! Şimdi enerjin yerinde."],
    "TOYS": ["Aferin, odan tertemiz!", "Harika, oyuncaklar evlerinde!", "Bravo! Çok düzenlisin.",
             "Süper, oyuncaklar teşekkür ediyor!", "Oley! Yarın hepsini kolayca bulacaksın.",
             "Aferin sana, ne güzel topladın!", "Çok güzel, her şey yerli yerinde!",
             "Harikasın! Seninle gurur duyuyorum.", "Tebrikler, çok iyi yaptın!",
             "Ne güzel! Oyuncaklar da uyuyabilir artık."],
    "TOILET": ["Aferin, uykuya hazırsın!", "Harika, şimdi rahat uyursun!", "Bravo! Çok akıllısın.",
               "Süper, aferin sana!", "Oley! Uyku vakti yaklaşıyor.", "Aferin sana, unutmadın!",
               "Çok güzel, rahat bir uyku seni bekliyor!", "Harikasın! Her şey tamam.",
               "Tebrikler, çok iyi yaptın!", "Ne güzel! Gece boyunca rahat edeceksin."],
}

NO = {
    "TEETH_AM": ["Hadi beraber fırçalamaya gidelim!", "Hadi, dişlerin seni bekliyor.",
                 "Fırçan seni çağırıyor, hadi gidelim!", "Hadi bakalım, iki dakika fırçalayalım.",
                 "Hadi, mikropları kovalım!", "Hadi koş, dişlerini fırçala, ben burada bekliyorum.",
                 "Dişler fırçalanınca çok güzel olur, hadi!", "Hadi, köpük köpük fırçalayalım!",
                 "Hadi gidelim, sonra bana anlatırsın.", "Fırçala gel, seni burada bekliyorum."],
    "DRESS": ["Hadi, pijamaları çıkaralım!", "Hadi bakalım, giyinme zamanı!", "Hadi, kıyafetlerin seni bekliyor.",
              "Hadi, önce bir tişört, sonra pantolon!", "Hadi giyin gel, seni bekliyorum.",
              "Pijamalar biraz dinlensin, hadi giyinelim!", "Hadi, en sevdiğin kıyafeti seç!",
              "Hadi, giyinince çok güzel olacaksın.", "Hadi, hızlıca giyinelim!",
              "Hadi bakalım, kim daha hızlı giyinecek?"],
    "SCHOOL_CLOTHES": ["Hadi, okul kıyafetlerini giyelim!", "Hadi bakalım, okula hazırlanma zamanı!",
                       "Hadi, okul kıyafetlerin seni bekliyor.", "Hadi giyin gel, seni bekliyorum.",
                       "Hadi, hızlıca giyinelim, okul başlamadan!", "Hadi, önce üstünü, sonra altını giy!",
                       "Hadi bakalım, kim daha hızlı?", "Hadi, giyinince çok şık olacaksın.",
                       "Hadi, arkadaşların seni bekliyor!", "Hadi, okul kıyafetini giy, sonra bana göster!"],
    "HAIR": ["Hadi, tarağı alalım!", "Hadi bakalım, saçlarını tarayalım!", "Hadi, saçların seni bekliyor.",
             "Hadi, birkaç tarak yeter!", "Hadi tara gel, seni bekliyorum.", "Hadi, istersen biri sana yardım etsin.",
             "Hadi, saçların düğüm olmasın!", "Hadi, taranınca çok güzel olacaksın.", "Hadi bakalım, tarak nerede?",
             "Hadi, saçlarını tara, sonra bana göster!"],
    "VITAMIN": ["Hadi, vitaminini içelim!", "Hadi, bir büyüğünden iste!", "Hadi bakalım, vitamin zamanı!",
                "Hadi, iç gel, seni bekliyorum.", "Hadi, vitaminler seni bekliyor.",
                "Hadi, annenden ya da babandan isteyelim!", "Hadi, içince çok güçlü olacaksın!",
                "Hadi, unutmayalım!", "Hadi bakalım, vitamin nerede?", "Hadi, iç sonra bana anlat!"],
    "BREAKFAST": ["Hadi, kahvaltı yapalım!", "Hadi bakalım, sofraya!", "Hadi, kahvaltı seni bekliyor.",
                  "Hadi, ye gel, seni bekliyorum.", "Hadi, birkaç lokma da olsa ye!",
                  "Hadi, kahvaltı yapınca çok güçlü olursun.", "Hadi, karnını doyuralım!", "Hadi, mutfağa gidelim!",
                  "Hadi, en sevdiğin kahvaltılığı ye!", "Hadi, ye sonra bana anlat!"],
    "WATER": ["Hadi, bir bardak su içelim!", "Hadi bakalım, su molası!", "Hadi, su seni bekliyor.",
              "Hadi, iç gel, seni bekliyorum.", "Hadi, birkaç yudum da olsa iç!",
              "Hadi, su içince çok iyi hissedersin.", "Hadi, mutfağa gidelim!", "Hadi, en güzel bardağını seç!",
              "Hadi, bence biraz susadın!", "Hadi, iç sonra bana anlat!"],
    "DINNER": ["Hadi, birkaç lokma daha!", "Hadi bakalım, yemeğini bitirelim!", "Hadi, yemeğin seni bekliyor.",
               "Hadi, ye gel, seni bekliyorum.", "Hadi, birkaç kaşık da olsa ye!",
               "Hadi, yemek yiyince çok güçlü olursun.", "Hadi, karnını doyuralım!", "Hadi, sofraya geri dönelim!",
               "Hadi, bir lokma senin için, bir lokma benim için!", "Hadi, ye sonra bana anlat!"],
    "TOYS": ["Hadi, oyuncakları toplayalım!", "Hadi bakalım, toplama yarışı!",
             "Hadi, oyuncaklar evlerine dönmek istiyor.", "Hadi, topla gel, seni bekliyorum.",
             "Hadi, önce bloklar, sonra bebekler!", "Hadi, şarkı söyleyerek toplayalım!", "Hadi, oyuncaklar da uyusun!",
             "Hadi bakalım, kim daha hızlı toplayacak?", "Hadi, birlikte yaparsak çabuk biter!",
             "Hadi, topla sonra bana anlat!"],
    "TOILET": ["Hadi, tuvalete gidelim!", "Hadi bakalım, uyku öncesi son durak!", "Hadi, gidip gel, seni bekliyorum.",
               "Hadi, gece rahat uyumak için!", "Hadi, hızlıca gidip gelelim!", "Hadi, sonra ellerini yıkamayı unutma!",
               "Hadi, uyku vakti yaklaşıyor!", "Hadi, gidince çok rahat edeceksin.", "Hadi bakalım, kim önce dönecek?",
               "Hadi, git sonra bana anlat!"],
}

# the open question at the end of the day: any answer is a good one
HAPPY_ASK = [
    "{name}, bugün seni en çok ne mutlu etti?", "{name}, bugünün en güzel anı neydi?",
    "{name}, bugün en çok neye güldün?", "{name}, bugün en çok ne hoşuna gitti?",
    "{name}, bugün seni ne sevindirdi?", "{name}, bugün en eğlenceli şey neydi?",
    "{name}, bugün seni gülümseten bir şeyi anlatır mısın?", "{name}, bugünün en güzel sürprizi neydi?",
    "{name}, bugün en çok neyi sevdin?", "{name}, bugün en çok ne yapmaktan keyif aldın?",
]
HAPPY_REPLY = [
    "Çok güzel, bunu duyduğuma sevindim! İyi geceler.", "Ne güzel! Tatlı rüyalar.",
    "Bayıldım! Yarın da güzel bir gün olsun. İyi geceler.", "Çok tatlı! Bunu ben de çok sevdim. İyi uykular.",
    "Harika! Senin mutluluğun beni de mutlu ediyor. İyi geceler.", "Ne hoş! Belki rüyanda da görürsün. Tatlı rüyalar.",
    "Çok güzelmiş! Anlattığın için teşekkür ederim. İyi geceler.", "Ooo, ne eğlenceli! İyi uykular.",
    "Ne güzel bir gün geçirmişsin! İyi geceler.", "Seni dinlemek çok güzel! Tatlı rüyalar.",
]
HAPPY_SILENT = ["Peki, yarın anlatırsın. İyi geceler!", "Sorun değil, yarın konuşuruz. Tatlı rüyalar.",
                "Tamam, belki yarın. İyi uykular!", "Peki tatlım, iyi geceler.", "Yarın bana anlatırsın, olur mu? İyi uykular.",
                "Tamam, şimdi dinlenme vakti. İyi geceler!", "Sorun değil, rüyanda düşünürsün. Tatlı rüyalar.",
                "Peki, ben de uykuya hazırım. İyi geceler!", "Tamam, yarın daha çok konuşuruz. İyi uykular.",
                "Peki, sıkıca sarıl ve uyu. İyi geceler!"]
# the first thing she says when the morning starts
GREET_AM = [
    "Günaydın {name}!", "Günaydın {name}, yeni bir gün başladı!", "Günaydın {name}, seni gördüğüme çok sevindim!",
    "Günaydın güneşim {name}!", "Günaydın {name}, bugün harika bir gün olacak!", "Günaydın {name}, ben de yeni uyandım!",
    "Günaydın {name}, haydi güne başlayalım!", "Günaydın tatlım {name}!", "Günaydın {name}, kahvaltı kokusu geliyor sanki!",
    "Günaydın {name}, seni çok özledim!",
]

assert all(len(YES[k]) == 10 and len(NO[k]) == 10 for k in YES), "10 answers each"
assert all(len(set(v)) == len(v) for v in list(YES.values()) + list(NO.values())), "no repeats within a list"
assert len(HAPPY_ASK) == len(HAPPY_REPLY) == len(UNCLEAR) == len(HAPPY_SILENT) == len(GREET_AM) == 10
assert all(len(q["ask"]) == 10 and len(set(q["ask"])) == 10 for q in QUESTIONS.values()), "10 ways to ask each"
