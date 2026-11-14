"""Her daily-routine questions and the answers she plays back, in Turkish.

Each question has a few ways of asking and 10 "yes" and 10 "no" answers; the toy cycles through them
so the same answer never comes twice in a row. {name} is the child's name from
~/.config/unicorn-pet/personal.json: the pack is private (fw/personal/, gitignored), so no child's
name is in this repo or in the public firmware. Built by `tools/voice.py --routines`.

SCHEDULE says when each question is asked (see README, "Her daily routine"): "am" = the hour after
wake-up, "pm" = from 90 to 30 minutes before bedtime, a time = that time; "school" = school days only,
"free" = weekends and holidays only, "weekend" = Saturday and Sunday, "sunday" = Sundays, "all" = every day;
"wash" = after she has had her bath, "suggest" = now and then at home, "chat" = now and then (not by the clock).
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
    # after she has had her own bath (the wash), sometimes: at most twice a week. late: its clips go after
    # the greetings in the pack, so a pack built before it keeps every other clip where it was
    "BATH": dict(when="wash", days="all", late=True, ask=[
        "Ben banyomu yaptım, mis gibi oldum! {name}, sen de banyonu yaptın mı?", "{name}, sen de banyo yaptın mı?",
        "Köpük köpük banyo yaptım! {name}, sen de yaptın mı?", "{name}, bugün banyonu yaptın mı?",
        "{name}, banyo yapınca çok güzel kokuyorum. Sen de banyonu yaptın mı?",
        "Tertemiz oldum! {name}, senin de banyon tamam mı?", "{name}, sen de benim gibi köpüklü banyo yaptın mı?",
        "Banyo çok eğlenceliydi! {name}, sen de yaptın mı?", "{name}, sen de yıkandın mı? Banyonu yaptın mı?",
        "Ben pırıl pırıl oldum! Ya sen {name}, banyonu yaptın mı?"]),
    # After HAPPY in the toy's order (after_happy) and at the end of the pack (late), so older saves and packs keep
    # every bit and clip where it was. star=False: not a habit (no star, not in the week's table).
    "BASKETBALL": dict(when="13:00", days="weekend", late=True, after_happy=True, star=False, ask=[
        "{name}, basketbola gidecek misin? Hazırlanma vakti!", "{name}, bugün basketbol var! Hazırlanıyor musun?",
        "Basketbol zamanı yaklaşıyor {name}! Gidecek misin?", "{name}, basket ayakkabıların hazır mı? Basketbola gidiyor musun?",
        "{name}, saat birde basketbol var. Gidecek misin?", "Top seni bekliyor {name}! Basketbola gidecek misin?",
        "{name}, hazırlanma vakti! Bugün basketbola gidiyor musun?", "{name}, basketbol çantan hazır mı? Gidecek misin?",
        "Hadi bakalım {name}, basketbola gidecek misin?", "{name}, bugün basket atmaya gidecek misin?"]),
    "VOLLEYBALL": dict(when="09:00", days="sunday", late=True, after_happy=True, star=False, ask=[
        "{name}, voleybola gidecek misin? Hazırlanma vakti!", "{name}, bugün voleybol var! Hazırlanıyor musun?",
        "Voleybol zamanı yaklaşıyor {name}! Gidecek misin?", "{name}, voleybol için hazır mısın? Gidiyor musun?",
        "{name}, bu sabah voleybol var. Gidecek misin?", "Top seni bekliyor {name}! Voleybola gidecek misin?",
        "{name}, hazırlanma vakti! Bugün voleybola gidiyor musun?", "{name}, voleybol çantan hazır mı? Gidecek misin?",
        "Günaydın {name}! Voleybola gidecek misin?", "{name}, bugün smaç atmaya gidecek misin?"]),
    # suggestions: now and then, only at home (when="suggest"); a yes sends her off to do the same thing
    "COLOR": dict(when="suggest", days="all", late=True, after_happy=True, star=False, ask=[
        "{name}, biraz boyama yapmak ister misin?", "{name}, boyalarını alıp bir resim boyayalım mı?",
        "Canım boyama yapmak istiyor! {name}, sen de ister misin?", "{name}, bugün bir şey boyamaya ne dersin?",
        "{name}, en sevdiğin renkle bir resim boyar mısın?", "{name}, boya kalemlerin seni özlemiş. Boyama yapalım mı?",
        "{name}, birlikte boyama yapalım mı?", "{name}, bir gökkuşağı boyamak ister misin?",
        "{name}, resim yapmaya ne dersin? Boyama ister misin?", "{name}, renkli bir resim boyayalım mı?"]),
    "LEGO": dict(when="suggest", days="all", late=True, after_happy=True, star=False, ask=[
        "{name}, legolarınla oynayalım mı?", "{name}, legolarla bir şey yapmak ister misin?",
        "{name}, legodan kocaman bir kule yapalım mı?", "{name}, legolarınla bir ev yapar mısın?",
        "Canım lego oynamak istiyor! {name}, sen de ister misin?", "{name}, legolarını çıkarıp oynayalım mı?",
        "{name}, legodan bana bir şey yapar mısın?", "{name}, bugün legolarla oynamaya ne dersin?",
        "{name}, legolarla bir araba yapalım mı?", "{name}, birlikte lego oynayalım mı?"]),
    "READ": dict(when="suggest", days="all", late=True, after_happy=True, star=False, ask=[
        "{name}, biraz kitap okumak ister misin?", "{name}, bir kitap okuyalım mı?",
        "{name}, en sevdiğin kitabı okumaya ne dersin?", "Canım masal dinlemek istiyor! {name}, kitap okur musun?",
        "{name}, bir kitabın resimlerine bakalım mı?", "{name}, bugün bir hikâye okumak ister misin?",
        "{name}, kitapların seni bekliyor. Okuyalım mı?", "{name}, birlikte kitap okuyalım mı?",
        "{name}, yeni bir hikâyeye ne dersin?", "{name}, biraz kitap keyfi yapalım mı?"]),
    "DOLLS": dict(when="suggest", days="all", late=True, after_happy=True, star=False, ask=[
        "{name}, çocuklarınla oynamak ister misin?", "{name}, çocuklarınla oynayalım mı?",
        "{name}, çocukların seni özlemiştir. Onlarla oynar mısın?", "{name}, çocuklarına bir oyun kuralım mı?",
        "Canım oyun oynamak istiyor! {name}, çocuklarınla oynayalım mı?", "{name}, çocuklarını gezdirmeye ne dersin?",
        "{name}, çocuklarınla bir çay partisi yapalım mı?", "{name}, bugün çocuklarınla oynamak ister misin?",
        "{name}, çocuklarını uyandırıp oynayalım mı?", "{name}, birlikte çocuklarınla oynayalım mı?"]),
    # now and then (when="chat"): an open question, any answer is a good one
    "HOWAREYOU": dict(when="chat", days="all", late=True, after_happy=True, star=False, kind="open", ask=[
        "{name}, nasılsın?", "{name}, bugün nasılsın?", "{name}, nasıl gidiyor?", "{name}, bugün kendini nasıl hissediyorsun?",
        "Merhaba {name}! Nasılsın?", "{name}, bana anlatır mısın, nasılsın?", "{name}, canın nasıl bugün?",
        "{name}, seni merak ettim. Nasılsın?", "{name}, bugün keyfin nasıl?", "{name}, neler yapıyorsun, nasılsın?"]),
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
    "BATH": ["Aferin, mis gibi kokuyorsun!", "Harika, ikimiz de tertemiziz!", "Bravo! Banyo yapmak çok güzel.",
             "Süper, pırıl pırıl olmuşsun!", "Oley! Artık köpük kardeşiyiz.", "Aferin sana, ne kadar temizsin!",
             "Çok güzel, şimdi rahat rahat oynarız!", "Harikasın! Tertemiz ve mis gibi.",
             "Tebrikler, çok iyi yaptın!", "Ne güzel! Banyodan sonra her şey daha güzel."],
    "BASKETBALL": ["Harika! İyi eğlenceler, bol bol basket at!", "Süper! Topu potaya gönder!", "Bravo! Çok eğleneceksin.",
                   "Oley! Su şişeni almayı unutma.", "Harika! Dönünce bana anlatırsın.", "Süpersin! Takımın seni bekliyor.",
                   "Ne güzel! Bugün çok basket atacaksın.", "Aferin! Hadi hazırlan bakalım.", "Harika, iyi oyunlar!",
                   "Çok güzel! Basketbol seni çok güçlü yapıyor."],
    "VOLLEYBALL": ["Harika! İyi eğlenceler, bol bol smaç!", "Süper! Topu fileden geçir!", "Bravo! Çok eğleneceksin.",
                   "Oley! Su şişeni almayı unutma.", "Harika! Dönünce bana anlatırsın.", "Süpersin! Takımın seni bekliyor.",
                   "Ne güzel! Bugün çok güzel oynayacaksın.", "Aferin! Hadi hazırlan bakalım.", "Harika, iyi oyunlar!",
                   "Çok güzel! Voleybol seni çok güçlü yapıyor."],
    "COLOR": ["Harika! Ben de boyama yapacağım, hadi başlayalım!", "Oley! Rengarenk bir resim olsun.", "Süper! Hangi renkle başlayacaksın?",
              "Bayıldım! Bitirince bana göster.", "Harika! Ben de bir gökkuşağı boyuyorum.", "Ne güzel! Boyalar hazır!",
              "Süpersin! Çok güzel bir resim olacak.", "Oley! Birlikte boyuyoruz!", "Harika! Bol renkli olsun.",
              "Çok güzel! Ben de boyamaya gidiyorum."],
    "LEGO": ["Harika! Ben de bir kule yapacağım!", "Oley! Kocaman bir şey yap!", "Süper! Ne yapacaksın acaba?",
             "Bayıldım! Bitirince bana göster.", "Harika! Ben de legolarla oynuyorum.", "Ne güzel! Parçaları hazırla!",
             "Süpersin! Çok güzel bir şey olacak.", "Oley! Birlikte yapıyoruz!", "Harika! En yüksek kuleyi kim yapacak?",
             "Çok güzel! Ben de lego oynamaya gidiyorum."],
    "READ": ["Harika! Ben de kitabımı okuyacağım!", "Oley! Güzel bir hikâye olsun.", "Süper! Hangi kitabı seçeceksin?",
             "Bayıldım! Sonra bana anlatırsın.", "Harika! Ben de bir masal okuyorum.", "Ne güzel! Kitabını al gel!",
             "Süpersin! Kitaplar çok güzel.", "Oley! Birlikte okuyoruz!", "Harika! Resimlerine de bak.",
             "Çok güzel! Ben de kitap okumaya gidiyorum."],
    "DOLLS": ["Harika! Ben de oyuncaklarımla oynayacağım!", "Oley! Çocukların çok sevinecek.", "Süper! Ne oynayacaksınız?",
              "Bayıldım! Sonra bana anlatırsın.", "Harika! Ben de oyun kuruyorum.", "Ne güzel! Çocuklarını topla gel!",
              "Süpersin! Çok güzel bir anne olacaksın.", "Oley! Birlikte oynuyoruz!", "Harika! İyi eğlenceler.",
              "Çok güzel! Ben de oynamaya gidiyorum."],
    "HOWAREYOU": ["Anlattığın için teşekkür ederim! Seni çok seviyorum.", "Seni dinlemek çok güzel! Ben hep yanındayım.",
                  "Teşekkürler! Seninle konuşmak beni çok mutlu ediyor.", "Anladım. Sen benim en iyi arkadaşımsın!",
                  "Peki! Ne olursa olsun ben hep buradayım.", "Teşekkür ederim! Sana kocaman bir sarılma gönderiyorum.",
                  "Ne güzel anlattın! Seni çok seviyorum.", "Anladım! İstediğin zaman bana anlatabilirsin.",
                  "Teşekkürler! Bugün de seninle olmak çok güzel.", "Peki tatlım! Sana kocaman bir kalp gönderiyorum."],
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
    "BATH": ["Hadi, sen de köpüklü bir banyo yap!", "Belki bu akşam banyo yaparsın, çok eğlenceli!",
             "Banyo yapınca mis gibi olursun!", "Annene ya da babana sorabilirsin, banyo zamanı mı?",
             "Köpüklerle oynamak çok güzel, bir denesene!", "Banyodan sonra bana anlatırsın, olur mu?",
             "Hadi bakalım, banyo seni bekliyor!", "Sıcacık bir banyo çok iyi gelir!",
             "Bir dahaki banyoda beraber köpük yapalım!", "Banyo yapınca gel, beraber mis gibi oynarız!"],
    "BASKETBALL": ["Peki, belki bir dahaki sefere!", "Tamam, bugün dinlen o zaman.", "Peki, haftaya belki.",
                   "Sorun değil, başka bir gün oynarız.", "Tamam! Evde de top oynayabilirsin.", "Peki, sen bilirsin.",
                   "Tamam, bugün başka bir şey yaparız.", "Peki! Belki sonra canın ister.", "Sorun değil, tatlım.",
                   "Tamam, ben buradayım."],
    "VOLLEYBALL": ["Peki, belki bir dahaki sefere!", "Tamam, bugün dinlen o zaman.", "Peki, haftaya belki.",
                   "Sorun değil, başka bir gün oynarız.", "Tamam! Evde de top oynayabilirsin.", "Peki, sen bilirsin.",
                   "Tamam, bugün başka bir şey yaparız.", "Peki! Belki sonra canın ister.", "Sorun değil, tatlım.",
                   "Tamam, ben buradayım."],
    "COLOR": ["Peki, belki sonra!", "Tamam, başka zaman boyarız.", "Sorun değil!", "Peki, istediğin zaman boyarız.",
              "Tamam tatlım.", "Peki, boyalar seni bekler.", "Sorun değil, başka bir şey yaparız.", "Tamam, belki yarın!",
              "Peki, sen bilirsin.", "Tamam, ben buradayım."],
    "LEGO": ["Peki, belki sonra!", "Tamam, başka zaman oynarız.", "Sorun değil!", "Peki, istediğin zaman oynarız.",
             "Tamam tatlım.", "Peki, legolar seni bekler.", "Sorun değil, başka bir şey yaparız.", "Tamam, belki yarın!",
             "Peki, sen bilirsin.", "Tamam, ben buradayım."],
    "READ": ["Peki, belki sonra!", "Tamam, başka zaman okuruz.", "Sorun değil!", "Peki, istediğin zaman okuruz.",
             "Tamam tatlım.", "Peki, kitaplar seni bekler.", "Sorun değil, başka bir şey yaparız.", "Tamam, belki yarın!",
             "Peki, sen bilirsin.", "Tamam, ben buradayım."],
    "DOLLS": ["Peki, belki sonra!", "Tamam, başka zaman oynarız.", "Sorun değil!", "Peki, istediğin zaman oynarız.",
              "Tamam tatlım.", "Peki, çocukların seni bekler.", "Sorun değil, başka bir şey yaparız.", "Tamam, belki yarın!",
              "Peki, sen bilirsin.", "Tamam, ben buradayım."],
    "HOWAREYOU": ["Peki, istediğin zaman anlatırsın. Ben buradayım!", "Sorun değil, sonra konuşuruz.",
                  "Tamam tatlım, ben hep yanındayım.", "Peki! Sana kocaman bir sarılma.", "Sorun değil, seni seviyorum!",
                  "Tamam, belki sonra anlatırsın.", "Peki, ben burada bekliyorum.", "Sorun değil, sen bilirsin.",
                  "Tamam! Sana bir kalp gönderiyorum.", "Peki tatlım, sonra konuşuruz."],
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
