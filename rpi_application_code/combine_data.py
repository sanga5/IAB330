#!/usr/bin/env python3
import csv

# Read your data and rename columns
your_data = []
with open('n11611553_CombinedTrainingData.csv', 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        # Rename columns
        row['meanAx'] = row.pop('meanX')
        row['sdAx'] = row.pop('sdX')
        row['rangeAx'] = row.pop('rangeX')
        row['meanAy'] = row.pop('meanY')
        row['sdAy'] = row.pop('sdY')
        row['rangeAy'] = row.pop('rangeY')
        row['meanAz'] = row.pop('meanZ')
        row['sdAz'] = row.pop('sdZ')
        row['rangeAz'] = row.pop('rangeZ')
        # Lowercase labels
        row['label'] = row['label'].lower()
        your_data.append(row)

# Read Bert's data
bert_data = []
with open('training_data_bert.csv', 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        row['label'] = row['label'].lower()
        bert_data.append(row)

# Combine
combined = your_data + bert_data

# Write combined
with open('combined_training_data.csv', 'w', newline='') as f:
    fieldnames = ['meanAx', 'sdAx', 'rangeAx', 'meanAy', 'sdAy', 'rangeAy', 
                  'meanAz', 'sdAz', 'rangeAz', 'meanGx', 'sdGx', 'rangeGx', 
                  'meanGy', 'sdGy', 'rangeGy', 'meanGz', 'sdGz', 'rangeGz', 'label', 'studentId']
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(combined)

print(f"Your data: {len(your_data)} samples")
print(f"Bert's data: {len(bert_data)} samples")
print(f"Combined: {len(combined)} samples")
print("Saved to combined_training_data.csv")
