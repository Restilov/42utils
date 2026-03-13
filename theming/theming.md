# Linux Theming Rehberi (42 Okul Ortamı)

Bu rehber, kısıtlı yetkilere sahip okul bilgisayarlarında (GNOME masaüstü) tema ve ikon özelleştirmelerini nasıl yapacağınızı anlatır.

## 1. Oomox (Themix) ile Tema Oluşturma

Kendi renk paletinize göre tema oluşturmak için **Oomox** (yeni adıyla Themix) kullanabilirsiniz. Root yetkisi gerekmediği için Flatpak üzerinden kullanıcı modunda kuracağız.

### Kurulum (Flatpak)
Önce Flatpak'in `/sgoinfre` üzerine kurulu olduğundan emin olun (bkz: `flatpak.md`).

```bash
# Oomox (Themix) GUI uygulamasını kur
flatpak --user install flathub com.gitlab.themix_project.Oomox
```

### Kullanım
1.  Uygulamayı başlatın (Menüden veya terminalden: `flatpak run com.gitlab.themix_project.Oomox`).
2.  Sol menüden bir hazır set seçin (örn: Numix).
3.  Renkleri zevkinize göre değiştirin.
4.  **Export Theme** butonuna basarak temayı oluşturun.

---

## 2. Tema ve İkon Klasörleri

İndirdiğiniz veya oluşturduğunuz temaları sistemin tanıması için ev dizininizde belirli klasörler olmalıdır. Eğer yoksa oluşturun:

```bash
# GTK ve Shell temaları için
mkdir -p ~/.themes

# İkon setleri ve cursor (imleç) temaları için
mkdir -p ~/.icons
```

> **Not:** Oomox genellikle temaları otomatik olarak `~/.themes` içine atar. İnternetten (örn: gnome-look.org) indirdiğiniz temaları zipten çıkarıp bu klasörlereatmalısınız.

---

## 3. Temayı Aktifleştirme (Tweaks & Settings)

Temaları uygulamak için **GNOME Tweaks** aracına ihtiyacınız var. Okul bilgisayarlarında genellikle yüklü gelir.

1.  **GNOME Tweaks** uygulamasını başlatın.
2.  **Appearance (Görünüm)** sekmesine gelin.
3.  **Applications (Uygulamalar):** Buradan oluşturduğunuz veya indirdiğiniz GTK temasını seçin.
4.  **Icons (İkonlar):** Buradan ikon setini seçin.
5.  **Cursor (İmleç):** Buradan imleç temasını seçin.
6.  **Shell:** GNOME üst bar ve menü temasını değiştirmek için **User Themes** eklentisi gereklidir (aşağıya bakın).

---

## 4. GNOME Extensions (Eklentiler)

Shell temasını (üst bar ve uygulama menüsü) değiştirebilmek için **User Themes** eklentisinin açık olması gerekir.

### Eklenti Yönetimi
1.  **Extensions (Eklentiler)** uygulamasını açın (yoksa tarayıcıdan `extensions.gnome.org` kullanabilirsiniz, ancak native host connector gerekebilir).
2.  **User Themes** eklentisini bulun ve **AÇIK** konuma getirin.
3.  Şimdi Tweaks -> Appearance -> Shell kısmından temayı değiştirebilirsiniz.

### Önerilen Eklentiler
*   **User Themes:** Shell teması yüklemek için zorunlu.
*   **Dash to Dock:** Dock çubuğunu özelleştirmek için.
*   **Blur My Shell:** Panelleri şeffaflaştırmak/bulanıklaştırmak için.
