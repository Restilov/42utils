# Flatpak Kurulumu & Sorun Giderme Rehberi

Bu rehber, Flatpak uygulamalarının disk kotasını doldurmaması için `/sgoinfre` altında nasıl yapılandırılacağını ve olası sorunların nasıl çözüleceğini anlatır.

## 1. İlk Kurulum ve Yapılandırma

Flatpak uygulamalarını ve verilerini `/sgoinfre` diskine taşımak için sembolik link (symlink) kullanacağız.

```bash
# 1. Mevcut flatpak klasörünü temizle (varsa)
rm -rf ~/.local/share/flatpak

# 2. sgoinfre üzerinde ana klasörü oluştur
mkdir -p /sgoinfre/$USER/flatpak

# 3. Sembolik linki oluştur
ln -s /sgoinfre/$USER/flatpak ~/.local/share/flatpak

# 4. Flathub deposunu ekle
flatpak --user remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
```

### Kurulum Kontrolü
Aşağıdaki komutla kurulumun doğru yapıldığını doğrulayabilirsiniz:
```bash
ls -la ~/.local/share/flatpak
```
**Beklenen Çıktı:**
```
lrwxrwxrwx ... /home/rakman/.local/share/flatpak -> /sgoinfre/rakman/flatpak
```
Bu çıktı, `.local/share/flatpak` yolunun aslında `/sgoinfre` diskini işaret ettiğini gösterir.

---

## 2. Homemover ile Birlikte Kullanım

Eğer `homemover.sh` scriptini çalıştırırsanız, `.local/share` klasörünü **taşımamanız** gerekir.

*   `homemover.sh` zaten `.local/share` için size soru sorar.
*   **Cevap:** `n` (Hayır) olmalıdır.
*   Çünkü `.local/share/flatpak` zaten bir symlink'tir. Eğer `.local/share` klasörünün tamamını taşırsanız, iç içe symlink oluşur ve Flatpak bozulabilir.

---

## 3. Uygulama Yükleme Örneği (Zen Browser)

Kurulum tamamlandıktan sonra uygulamaları normal şekilde kurabilirsiniz. Tüm veriler otomatik olarak sgoinfre'ye yazılacaktır.

```bash
flatpak --user install flathub app.zen_browser.zen
```

Çalıştırmak için:
```bash
flatpak run app.zen_browser.zen
```

---

## 4. Sorun Giderme (Tamamen Sıfırlama)

Eğer Flatpak bozulursa (örneğin "No remote refs found" hatası alırsanız veya yanlış symlink oluşursa), aşağıdaki adımlarla tamamen sıfırdan kurabilirsiniz:

```bash
# 1. Tüm kullanıcı uygulamalarını kaldır
flatpak --user uninstall --all

# 2. Bozuk veya yanlış linklenmiş yerel klasörü zorla sil
rm -rf ~/.local/share/flatpak

# 3. sgoinfre'deki eski kalıntıları temizle ve yeniden oluştur
rm -rf /sgoinfre/$USER/flatpak
mkdir -p /sgoinfre/$USER/flatpak

# 4. Doğru sembolik linki tekrar oluştur
ln -s /sgoinfre/$USER/flatpak ~/.local/share/flatpak

# 5. Flathub deposunu tekrar ekle (kritik adım)
flatpak --user remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

# 6. Depoların listelendiğini kontrol et
flatpak --user remotes
```
Bu adımdan sonra kurulum tekrar sorunsuz çalışacaktır.
