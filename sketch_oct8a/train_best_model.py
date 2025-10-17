"""
Train multiple models and find the best one for motion classification
Automatically handles data cleaning and model selection
"""
import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split, cross_val_score
from sklearn.preprocessing import StandardScaler, LabelEncoder
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score
import joblib
import sys

print("=" * 80)
print("🤖 AUTO MODEL SELECTION - Finding Best Classifier for Motion Data")
print("=" * 80)

# Load and clean data
print("\n📂 Loading data...")
data = pd.read_csv('n11611553_CombinedTrainingData.csv')

# Check available columns
print(f"📋 Available columns: {list(data.columns)}")

# Define feature columns - using accelerometer and gyroscope statistics
feature_columns = ['meanX', 'sdX', 'rangeX', 'meanY', 'sdY', 'rangeY', 
                   'meanZ', 'sdZ', 'rangeZ', 'meanGx', 'sdGx', 'rangeGx',
                   'meanGy', 'sdGy', 'rangeGy', 'meanGz', 'sdGz', 'rangeGz']

# Remove rows with missing values in feature columns
data_clean = data[data[feature_columns].notna().all(axis=1)].copy()

# Remove rows where ALL accelerometer features are zero (indicates bad read)
accel_cols = ['meanX', 'sdX', 'rangeX', 'meanY', 'sdY', 'rangeY', 'meanZ', 'sdZ', 'rangeZ']
data_clean = data_clean[~((data_clean[accel_cols] == 0).all(axis=1))]

print(f"✅ Loaded {len(data_clean)} samples (removed {len(data) - len(data_clean)} corrupted)")
print(f"📊 Classes: {list(data_clean['label'].value_counts().index)}")
print(f"📊 Using {len(feature_columns)} features: Accelerometer (9) + Gyroscope (9)")

# Prepare data
X = data_clean[feature_columns]
y = data_clean['label']

label_encoder = LabelEncoder()
y_encoded = label_encoder.fit_transform(y)

X_train, X_test, y_train, y_test = train_test_split(
    X, y_encoded, test_size=0.2, random_state=42, stratify=y_encoded
)

scaler = StandardScaler()
X_train_scaled = scaler.fit_transform(X_train)
X_test_scaled = scaler.transform(X_test)

# Try models
models = {}

print("\n" + "=" * 80)
print("🔄 Training Models...")
print("=" * 80)

# 1. Random Forest (with 18 features)
print("\n1️⃣  Random Forest Classifier...")
rf_model = RandomForestClassifier(
    n_estimators=150,
    max_depth=12,
    min_samples_split=4,
    min_samples_leaf=2,
    random_state=42,
    n_jobs=-1
)
rf_model.fit(X_train, y_train)
rf_pred = rf_model.predict(X_test)
rf_acc = accuracy_score(y_test, rf_pred)
rf_cv = cross_val_score(rf_model, X_train, y_train, cv=5).mean()
models['Random Forest'] = {
    'model': rf_model,
    'accuracy': rf_acc,
    'cv_score': rf_cv,
    'predictions': rf_pred,
    'uses_scaler': False
}
print(f"   ✅ Accuracy: {rf_acc:.4f} | CV Score: {rf_cv:.4f}")

# 2. XGBoost (if available)
try:
    import xgboost as xgb
    print("\n2️⃣  XGBoost Classifier...")
    xgb_model = xgb.XGBClassifier(
        n_estimators=100,
        max_depth=6,
        learning_rate=0.1,
        random_state=42,
        n_jobs=-1,
        eval_metric='mlogloss'
    )
    xgb_model.fit(X_train, y_train)
    xgb_pred = xgb_model.predict(X_test)
    xgb_acc = accuracy_score(y_test, xgb_pred)
    xgb_cv = cross_val_score(xgb_model, X_train, y_train, cv=5).mean()
    models['XGBoost'] = {
        'model': xgb_model,
        'accuracy': xgb_acc,
        'cv_score': xgb_cv,
        'predictions': xgb_pred,
        'uses_scaler': False
    }
    print(f"   ✅ Accuracy: {xgb_acc:.4f} | CV Score: {xgb_cv:.4f}")
except ImportError:
    print("\n2️⃣  XGBoost - Not installed (skipping)")

# 3. SVM with RBF (for comparison) - uses all 18 features
from sklearn.svm import SVC
print("\n3️⃣  SVM with RBF Kernel...")
svm_model = SVC(kernel='rbf', C=50, gamma='scale', random_state=42, probability=True)
svm_model.fit(X_train_scaled, y_train)
svm_pred = svm_model.predict(X_test_scaled)
svm_acc = accuracy_score(y_test, svm_pred)
svm_cv = cross_val_score(svm_model, X_train_scaled, y_train, cv=5).mean()
models['SVM RBF'] = {
    'model': svm_model,
    'accuracy': svm_acc,
    'cv_score': svm_cv,
    'predictions': svm_pred,
    'uses_scaler': True
}
print(f"   ✅ Accuracy: {svm_acc:.4f} | CV Score: {svm_cv:.4f}")

# Find best model
print("\n" + "=" * 80)
print("🏆 MODEL COMPARISON")
print("=" * 80)
best_name = max(models.keys(), key=lambda k: models[k]['accuracy'])
print(f"\n{'Model':<20} {'Test Accuracy':<15} {'CV Score':<15}")
print("-" * 50)
for name, metrics in models.items():
    marker = "🌟 BEST" if name == best_name else ""
    print(f"{name:<20} {metrics['accuracy']:.4f}          {metrics['cv_score']:.4f}          {marker}")

best_model_obj = models[best_name]
best_pred = best_model_obj['predictions']
uses_scaler = best_model_obj['uses_scaler']

print(f"\n✅ Best Model: {best_name}")
print(f"   Test Accuracy: {best_model_obj['accuracy']:.4f}")
print(f"   Uses Scaler: {uses_scaler}")

# Detailed classification report
print("\n" + "=" * 80)
print(f"DETAILED RESULTS - {best_name}")
print("=" * 80)
print("\nClassification Report:")
print(classification_report(y_test, best_pred, target_names=label_encoder.classes_))

print("\nConfusion Matrix:")
cm = confusion_matrix(y_test, best_pred)
print(cm)

# Feature importance (for tree-based models)
if hasattr(best_model_obj['model'], 'feature_importances_'):
    print("\nTop 8 Most Important Features:")
    importances = best_model_obj['model'].feature_importances_
    indices = np.argsort(importances)[::-1][:8]
    for i, idx in enumerate(indices, 1):
        print(f"   {i}. {feature_columns[idx]}: {importances[idx]:.4f}")
else:
    print("\n(Feature importance not available for this model type)")

# Save the best model
print("\n" + "=" * 80)
print("💾 SAVING MODELS")
print("=" * 80)

# Save best model
joblib.dump(best_model_obj['model'], 'best_motion_classifier.pkl')
joblib.dump(label_encoder, 'label_encoder.pkl')
if uses_scaler:
    joblib.dump(scaler, 'feature_scaler.pkl')
else:
    print("✅ Saved: best_motion_classifier.pkl")
    print("✅ Saved: label_encoder.pkl")
    print("   (No scaler needed for this model)")

print("\n" + "=" * 80)
print(f"✨ SUCCESS! Best model ({best_name}) trained and saved!")
print("=" * 80)
print("\n📝 To use the new model, update live_predictor.py to load:")
print("   - best_motion_classifier.pkl (instead of svm_motion_classifier.pkl)")
print("   - Only use scaler if you see 'Uses Scaler: True' above")
