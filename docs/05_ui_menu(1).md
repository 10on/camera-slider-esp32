# Camera Slider — UI & Menu Specification (v1.1)

## 1. Общие принципы

- UI не управляет мотором напрямую
- UI работает через системную логику
- UI не нарушает state machine

---

## 2. Частота обновления

- Рекомендуемая частота: 10–20 Гц
- Максимум: 30 Гц
- Обновление только при изменениях

---

## 3. Главный экран

Отображает:
- позицию
- состояние
- скорость
- BLE статус
- ошибку

Энкодер:
- поворот — скорость
- нажатие — меню

---

## 4. Главное меню

Manual Move  
Go to Position  
Programs  
Calibration  
Settings  

---

## 5. Manual Move

- направление
- скорость
- START / STOP

Поведение на концевиках определяется Endstop Mode.

---

## 6. Settings

### 6.1. Motion Settings

- Speed
- Acceleration
- Microsteps
- Endstop Mode

### 6.2. Sleep Settings

- Sleep Timeout
- ADXL Sensitivity
- Wake on Motion (опционально)

### 6.3. System

- Motor Current
- Reset Calibration
- Reset Error
