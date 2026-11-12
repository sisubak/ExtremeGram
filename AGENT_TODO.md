# ExtremeGram — рабочий контекст и TODO

> Файл для сохранения контекста между сессиями/компактами.
> Читать вместе с AGENTS.md. Обновлять после каждой завершённой задачи.

## Идея проекта
ExtremeGram — форк tdesktop (Telegram Desktop) с расширенными инструментами
модерации и кастомизацией интерфейса. Ветка: `feature/extremegram-redesign`.
Репозиторий: `sisubak/ExtremeGram` (origin). Основная работа ведётся в
`Telegram/SourceFiles/custom/` + точечные правки upstream-файлов.

## Ключевые фичи форка
- Bot Mode: команды в чате `!mute`, `!ban`, `!stats`, `!help`, `!stop`, `!whois`
  (bot_mode_manager.{h,cpp}). `!whois` = ID + приблизительная дата регистрации
  по ID (kIdBirthMarks, якорь 1.766B ≈ 15.01.2026, экстраполяция для новых ID)
  + точная дата входа в группу через channels.GetParticipant.
- EnhancedMuteDialog (extremegram_mute_box.{h,cpp}): парсинг срока из текста
  (сек/мин/час/день/нед/год), причина, авто-сообщение «Ты был замучен».
  Вход: из меню «Ограничить» (window_peer_menu.cpp ShowSenderRestrictions)
  и из moderate_messages_box.cpp (строка-ссылка «Ограничить через улучшенный
  диалог...»), только если опция ExtremeMuteDialog включена.
- Client Badge Detector (client_badge_detector.{h,cpp}): маркер в аватарке
  (kMarkerR/G/B), badgeWidth()=20, drawBadgeGetWidth() рисует тинтованную
  иконку (padLeft 3 + 14px + 2). Показывается: у имён в сообщениях
  (history_view_message.cpp ~2617), в списке чатов (dialogs/ui/dialogs_layout.cpp
  ~933 — добавлено 53b41f4), в списке участников (info/profile/
  info_profile_members_controllers.cpp MemberListRow::paintNameIconGetWidth —
  добавлено 53b41f4), в профиле (info_profile_top_bar.cpp ~587), в шапке
  1:1-звонка (calls/calls_top_bar.cpp _extremeBadge, после центрированного
  имени, только если влезает).
  ПКМ по сообщению → «Сведения о клиенте ExtremeGram» → бокс (имя жирным,
  @username, ID + кнопка «Копировать ID», регистрация, вход в группу,
  поясняющий текст) — history_view_context_menu.cpp AddExtremeBadgeAction.
  Клик по бейджу в профиле/звонке → анимированный попап
  (custom/client_badge_popup.{h,cpp}, ShowClientBadgePopup): карточка
  сверху по центру окна, иконка 36px, «ExtremeGram» + подпись, show 240мс
  (fade+scale+slide), hold 2.6с, hide 170мс, клик закрывает.
  badgePreview() в детекторе — QImage для произвольного размера/фона/тинта.
- Slowmode tracker (slowmode_tracker.cpp): Custom::SlowmodeFor(session) —
  рисует dial-иконку таймера у имён ограниченных юзеров в сообщениях.
- Typing indicator (typing_indicator.{h,cpp}): полоса «печатает…» над полем
  ввода; премиум/NFT статус-эмодзи (Data::EmojiStatusCustomId), аватары,
  акцентная заливка из DesignTokens.
- Redesign (redesign.{h,cpp}): режимы Classic/Modern/Ultra через DesignTokens
  (radiusId, cardAlpha, accentWashAlpha, accent) + TokensChanged()/TokensStream.
  ПОКА меняет только цвета/радиусы/заливки — НЕ layout (см. TODO п.6).
- EnhancedSettings (core/enhanced_settings.{h,cpp}): опции форка. Порядок
  OptionId и дескрипторов позиционный — новые опции добавлять в конец
  (перед Count) и дескриптор в ту же позицию.
- Online spam interval: api_updates.cpp updateOnline — интервал «печатает…»
  (OptionId::OnlineSpamInterval, слайдер в settings_extremegram.cpp
  BuildOnlineSpamInterval).
- Настройки форка: settings/sections/settings_extremegram.cpp (кнопки,
  слайдеры, about-тексты через builder.addDividerText).

## Сборка и CI
- Локально НЕ собрать: сабмодули пустые, нет Qt-заголовков. Проверка только CI.
- CI: workflow_dispatch `extremegram-build.yml`, ветка
  `feature/extremegram-redesign`. Токен: /tmp/opencode/ghtoken (если пропал —
  спросить у пользователя новый PAT). Dispatch:
  curl -X POST -H "Authorization: token $(cat /tmp/opencode/ghtoken)" \
    https://api.github.com/repos/sisubak/ExtremeGram/actions/workflows/extremegram-build.yml/dispatches \
    -d '{"ref":"feature/extremegram-redesign"}'
- Сборка ~1.5–2 ч. Успех → релиз `build-<run_id>` с AppImage + sha256.
- Логи упавшей сборки: jobs → /actions/jobs/<job_id>/logs, grep "error:".
- Пуш иногда падает "Recv failure" — повторить через sleep 40–120.

## Уроки компиляции (дорого добытые)
- ButtonArgs designated initializers: строго порядок объявления полей
  (.id,.title,.st,.icon,.container,.label,.toggled,.onClick,.keywords,...),
  иначе непонятная ошибка brace-init.
- Lang: `{count}` только в plural-ключах (иначе langgen error 806);
  для не-plural — `{amount}` + `lt_amount`; tr::phrase<lngtag_amount>(tr::now)
  возвращает QString, флаги (RichLangValue) НЕ передавать; для producer —
  оборачивать в rpl::single().
- SessionController::uiShow() → shared_ptr<ChatHelpers::Show>;
  GenericBox::uiShow() → shared_ptr<Ui::Show> (showToast/showBox есть).
- MTPDchannelParticipantCreator НЕ имеет vdate (есть у Participant/Admin/
  Banned/Self) — для создателя дату входа не показывать (FormatJoinDateTime(-1)
  → «неизвестно»).
- Новые custom-файлы регистрировать в Telegram/CMakeLists.txt (~606–619).
- `addAction(text, lambda, &st::menuIconInfo)` — валидный порядок аргументов.
- box->addButton(rpl::single(QString), fn) — для произвольного текста.
- Ui::FlatLabel НЕ имеет ctor с TextWithEntities — только
  rpl::producer<TextWithEntities>: оборачивать rpl::single(Ui::Text::Bold/Link(...)).
- Если в lang-строке убрал {amount} — убрать и lt_amount из вызова phrase,
  иначе "no match for call to (const tr::phrase<>)". 
- TopBar::paintEdges(p, clip, const QBrush&) — 3-арг перегрузка существует,
  style::color туда конвертируется (использовано в upstream).
- В dialogs_inner_widget pinned-строки лежат в _shownList по индексам
  [fixedOnTopCount(), +pinnedCount), а НЕ с нуля: row->index()-promoted =
  индекс в _pinnedRows.
- ClientBadgeDetector::badgeWidth() возвращает 0 при отсутствии бейджа —
  можно использовать как «резерв+флаг» без hasCustomClientBadge.
- В PaintRow список чатов бейдж-резерв клиента должен идти ДО paintPeerBadge,
  иначе verified-бейдж рисуется в освобождённый слот клиентского.
- object_ptr<T> НЕ имеет дефолтного конструктора — член класса объявлять
  `object_ptr<T> _x = { nullptr };`, иначе ошибка в конце init-list ctor.
- not_null<T*> не конвертируется неявно в T* — использовать `.get()`
  (например, SessionController::widget() → MainWindow* → QWidget*).
- ::MainWindow объявлен только forward-decl в window_session_controller.h:
  для widget()->... или конверсии в QWidget* нужен include "mainwindow.h"
  (он же тянет platform_main_window.h).
- Ui::Animations::Simple в запиненном lib_ui (c3f7343) прыгает в конец при
  anim::Disabled() — см. пункт 18 (Power Saving).

## Текущее состояние (на момент последнего обновления)
- HEAD: 9956945 (код — b0e28bc). Ветка запушена, рабочее дерево чистое.
- Последний зелёный релиз: build-43110062477 (b0e28bc) — скачан в
  /tmp/opencode/extre_43110062477, sha256 совпадает, ждёт установки
  пользователем. Включает: попап бейджа, бейдж в шапке звонка, анимацию
  изменения сообщений, клик по бейджу в сообщениях, whois-датасет.
  Предыдущие релизы build-43061945603 и build-43041765462 тоже лежат
  в /tmp/opencode/extre_*.
- Попап бейджа, бейдж в шапке звонка, анимация изменения сообщений и клик
  по бейджу в сообщениях закоммичены (6a684c0, ffc7d11, 11593f5).
- FAILED 43066932523 (6a684c0) и 43069241136 (ffc7d11): object_ptr без
  дефолтного ctor в calls_top_bar.cpp и not_null<MainWindow*> → QWidget*
  в history_view_message.cpp. Исправлено в cc3ccbe и eeeb56d.
- FAILED 43091534460 (eeeb56d): та же конверсия, но из-за неполного типа
  ::MainWindow (виден только forward-decl из window_session_controller.h).
  Фикс b0e28bc: include "mainwindow.h" в history_view_message.cpp.
- DesignTokens: флаги chatListCards/denseChatList/cardProfile/minimalProfile.
  Modern = карточки в списке + карточка-оверлей в профиле; Ultra = плотный
  список (56px только для обычных строк) + плоский профиль.

## TODO (актуальные баги и задачи от пользователя)
1. [x] Typing bar: эмодзи «улетал вбок» — откат 2a15d51 (64b0265), снова
   сразу после аватарок, перед текстом.
2. [x] Иконка таймера slowmode: откат ca8e89d + дуга и стрелка против часовой
   (span отрицательный: -ratio*360*16 от 90*16; стрелка θ = (0.5 - 2*ratio)π
   — совпадает с концом дуги и идёт 12→9→6→3).
3. [x] Бустеры и slowmode: эвристика — повторная отправка до истечения своего
   таймера => exempt (base::flat_set _exempt), таймер снимается. Точное
   определение через premium.getBoostsList — на будущее.
4. [x] Бейдж ExtremeGram в списке чатов и списке участников — сделано
   в 53b41f4 (проверить на сборке 42815391261).
5. [x] Online spam interval: 0 = максимально быстро, пол 10 мс
   (api_updates.cpp kMinimumInterval); максимум 600000 мс; about-текст
   предупреждает про flood-wait.
6. [x] Redesign Modern/Ultra: Modern — карточки в списке чатов + карточка-
   оверлей в профиле; Ultra — плотный список + плоский профиль (paintEdges).
7. [x] Тинт всех кастомных иконок форка под тему/фон (53b41f4, закрыто).
8. [x] «Ограничить через улучшенный диалог...» — причина: FlatLabel ловит
   клики только по link-энтити. Текст обёрнут в Ui::Text::Link.
9. [x] Создан этот файл (AGENT_TODO.md).
10. [x] !whois: дата регистрации в будущем. Причина: линейная интерполяция
    по последним двум меткам (ID-плотность выросла) уезжала за now.
    Фикс: clamp в min(now) + темп по всей базовой линии (первая-последняя
    метка), старые ID 1-2 остаются на старом темпе.
11. [x] Плотный список: строки с тегами/форумом больше не сжимаются до 56px
    на широком layout (только обычные строки; в narrow — интерполяция).
12. [x] Пересчёт при смене режима: TokensChanged теперь пересчитывает и
    community-высоты, и отфильтрованный список (refreshFilterResults),
    а не только _shownList.
13. [x] Геометрия при плотном списке: pinned drag (top() строк вместо
    minHeight), selectSkipPage (реальная высота строки), preloadRowsData
    (_shownList->height()), corner-status (FilterResult::top).
14. [x] Бейджи: резерв места в списке чатов до paintPeerBadge; clamp в
    списке участников; hit-test в сообщениях совпадает с отрисовкой
    (client badge + slowmode icon); карточка профиля рисуется оверлеем
    (без setClipPath), Ultra-профиль — paintEdges(st::windowBg).
15. [x] Анимированный попап бейджа вместо toast в профиле + клик по бейджу
    в шапке 1:1-звонка (custom/client_badge_popup.{h,cpp}).
16. [x] Анимация изменения сообщений: wash цвета msgReplyBarColor 900мс,
    хук Element::animateEdit() из HistoryItem::finishEdition (view при
    edit пересоздаётся, поэтому состояние живёт в view).
17. [~] Клик по бейджу: в сообщениях сделан (getStateFromName → отдельный
    LambdaClickHandler → ShowClientBadgePopup через tryResolveWindow);
    в списке участников/чатов — пока нет. Аудит-фиксы: _previousMode
    в history_view_reply.h инициализирован (= false, было UB);
    computeIsAttachToPrevious/displayFromName совпадают с upstream — не
    трогать; рассинхрон 19/20px — резерв badgeWidth() 20 vs рисование 19,
    оставлено (1px зазор перед slowmode-иконкой).
18. [x] Баг «плавные анимации становятся резкими при звонке» — решено:
    в запиненном lib_ui (c3f7343) Simple::BasicCallback ПРОВЕРЯЕТ
    anim::Disabled() и прыгает в конец; флаг ставит только
    PowerSaving::Set/SetForceAll (ui/power_saving.cpp). Причина у
    пользователя: включён чекбокс «Disable UI animations» в
    Settings → Advanced → Power Saving. Решение: снять галочку.
    Код звонков PowerSaving не трогает (проверено grep-ом).
19. [x] Появление окна 1:1-звонка: fade-in 150мс (window opacity,
    CubicSmoothOut) в Panel::initGeometry, только если !anim::Disabled().
    На Wayland setWindowOpacity игнорируется — окно просто появляется.

## Открытые вопросы
- Нет. Решения: Modern = карточки (список чатов + профиль), Ultra = плотный
  компакт; иконки форка тинтовать под тему/фон; interval 0 = пол 10 мс
  (пользователь принял риск flood-wait).

## Правила работы
- Стиль: tdesktop (CamelCase, auto, u"..."_q, crl::time в anon namespace).
- Без комментариев в коде, без эмодзи, минимальные диффы.
- Не переписывать файлы целиком без необходимости.
- Суб-агенты разрешены по прямой просьбе пользователя (в остальное время —
  только при явной необходимости и с проверкой их диффов).
- Пользователь тестирует AppImage сам; живые инстансы не трогать без просьбы.
